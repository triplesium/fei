#include <CLI/CLI.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <spirv_cross/spirv_cross.hpp>
#include <spirv_cross/spirv_glsl.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::filesystem::path input;
    std::filesystem::path glsl;
    std::filesystem::path reflect;
};

struct ReflectedResource {
    std::string name;
    std::string type;
    uint32_t set;
    uint32_t binding;
    std::vector<uint32_t> array;
};

void configure_options(CLI::App& app, Options& options) {
    app.add_option("--input", options.input, "Input SPIR-V file")
        ->required()
        ->check(CLI::ExistingFile);
    app.add_option("--glsl", options.glsl, "Output OpenGL GLSL file")
        ->required();
    app.add_option("--reflect", options.reflect, "Output reflection JSON file")
        ->required();
}

std::vector<uint32_t> read_spirv(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error(
            "failed to open SPIR-V input: " + path.string()
        );
    }

    auto size = input.tellg();
    if (size < 0 || size % 4 != 0) {
        throw std::runtime_error("invalid SPIR-V byte size: " + path.string());
    }

    std::vector<uint32_t> words(static_cast<size_t>(size) / sizeof(uint32_t));
    input.seekg(0);
    input.read(
        reinterpret_cast<char*>(words.data()),
        static_cast<std::streamsize>(size)
    );
    if (!input) {
        throw std::runtime_error(
            "failed to read SPIR-V input: " + path.string()
        );
    }
    return words;
}

void write_text(const std::filesystem::path& path, const std::string& content) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to open output: " + path.string());
    }
    output << content;
    if (!output) {
        throw std::runtime_error("failed to write output: " + path.string());
    }
}

std::string escape_json(std::string_view value) {
    std::string result;
    for (char c : value) {
        switch (c) {
            case '\\':
                result += "\\\\";
                break;
            case '"':
                result += "\\\"";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += c;
                break;
        }
    }
    return result;
}

std::string execution_model_name(spv::ExecutionModel model) {
    switch (model) {
        case spv::ExecutionModelVertex:
            return "vert";
        case spv::ExecutionModelFragment:
            return "frag";
        case spv::ExecutionModelGeometry:
            return "geom";
        case spv::ExecutionModelGLCompute:
            return "comp";
        case spv::ExecutionModelTessellationControl:
            return "tesc";
        case spv::ExecutionModelTessellationEvaluation:
            return "tese";
        default:
            return "unknown";
    }
}

std::string image_dim_name(spv::Dim dim) {
    switch (dim) {
        case spv::Dim1D:
            return "1D";
        case spv::Dim2D:
            return "2D";
        case spv::Dim3D:
            return "3D";
        case spv::DimCube:
            return "Cube";
        case spv::DimRect:
            return "Rect";
        case spv::DimBuffer:
            return "Buffer";
        case spv::DimSubpassData:
            return "SubpassData";
        default:
            return "Unknown";
    }
}

std::string resource_type_name(
    const spirv_cross::Compiler& compiler,
    const spirv_cross::Resource& resource
) {
    const auto& type = compiler.get_type(resource.type_id);
    if (type.basetype == spirv_cross::SPIRType::Struct) {
        const auto& name = compiler.get_name(resource.base_type_id);
        return name.empty() ? ("_" + std::to_string(resource.base_type_id)) :
                              name;
    }
    if (type.basetype == spirv_cross::SPIRType::Sampler) {
        return "sampler";
    }

    std::string prefix;
    if (type.basetype == spirv_cross::SPIRType::SampledImage) {
        prefix = "sampler";
    } else if (type.basetype == spirv_cross::SPIRType::Image) {
        prefix = "image";
    } else {
        return "unknown";
    }

    const auto& image = type.image;
    return prefix + image_dim_name(image.dim);
}

uint32_t decoration_or_zero(
    const spirv_cross::Compiler& compiler,
    spirv_cross::ID id,
    spv::Decoration decoration
) {
    if (!compiler.has_decoration(id, decoration)) {
        return 0;
    }
    return compiler.get_decoration(id, decoration);
}

ReflectedResource reflect_resource(
    const spirv_cross::Compiler& compiler,
    const spirv_cross::Resource& resource
) {
    const auto& type = compiler.get_type(resource.type_id);
    return ReflectedResource {
        .name = resource.name.empty() ?
                    compiler.get_fallback_name(resource.id) :
                    resource.name,
        .type = resource_type_name(compiler, resource),
        .set = decoration_or_zero(
            compiler,
            resource.id,
            spv::DecorationDescriptorSet
        ),
        .binding =
            decoration_or_zero(compiler, resource.id, spv::DecorationBinding),
        .array = std::vector<uint32_t>(type.array.begin(), type.array.end()),
    };
}

std::vector<ReflectedResource> reflect_resources(
    const spirv_cross::Compiler& compiler,
    const spirv_cross::SmallVector<spirv_cross::Resource>& resources
) {
    std::vector<ReflectedResource> reflected;
    reflected.reserve(resources.size());
    for (const auto& resource : resources) {
        reflected.push_back(reflect_resource(compiler, resource));
    }
    return reflected;
}

void unset_descriptor_layouts(
    spirv_cross::Compiler& compiler,
    const spirv_cross::SmallVector<spirv_cross::Resource>& resources
) {
    for (const auto& resource : resources) {
        compiler.unset_decoration(resource.id, spv::DecorationDescriptorSet);
        compiler.unset_decoration(resource.id, spv::DecorationBinding);
    }
}

void write_resource_array(
    std::ostringstream& json,
    std::string_view name,
    const std::vector<ReflectedResource>& resources,
    bool& first_property
) {
    if (resources.empty()) {
        return;
    }

    if (!first_property) {
        json << ",\n";
    }
    first_property = false;

    json << "    \"" << name << "\" : [\n";
    for (size_t i = 0; i < resources.size(); ++i) {
        const auto& resource = resources[i];
        json << "        {\n";
        json << R"(            "type" : ")" << escape_json(resource.type)
             << R"(",)" << '\n';
        json << R"(            "name" : ")" << escape_json(resource.name)
             << R"(",)" << '\n';
        json << "            \"set\" : " << resource.set << ",\n";
        json << "            \"binding\" : " << resource.binding;
        if (!resource.array.empty()) {
            json << ",\n            \"array\" : [";
            for (size_t j = 0; j < resource.array.size(); ++j) {
                if (j != 0) {
                    json << ", ";
                }
                json << resource.array[j];
            }
            json << "]\n";
        } else {
            json << "\n";
        }
        json << "        }";
        if (i + 1 != resources.size()) {
            json << ",";
        }
        json << "\n";
    }
    json << "    ]";
}

std::string make_reflection_json(spirv_cross::CompilerGLSL& compiler) {
    const auto shader_resources = compiler.get_shader_resources();
    const auto entry_points = compiler.get_entry_points_and_stages();

    const auto ubos =
        reflect_resources(compiler, shader_resources.uniform_buffers);
    const auto ssbos =
        reflect_resources(compiler, shader_resources.storage_buffers);
    const auto textures =
        reflect_resources(compiler, shader_resources.sampled_images);
    const auto separate_images =
        reflect_resources(compiler, shader_resources.separate_images);
    const auto images =
        reflect_resources(compiler, shader_resources.storage_images);
    const auto separate_samplers =
        reflect_resources(compiler, shader_resources.separate_samplers);

    std::ostringstream json;
    json << "{\n";
    json << "    \"entryPoints\" : [";
    for (size_t i = 0; i < entry_points.size(); ++i) {
        const auto& entry_point = entry_points[i];
        if (i != 0) {
            json << ",";
        }
        json << "\n        {\n";
        json << R"(            "name" : ")" << escape_json(entry_point.name)
             << R"(",)" << '\n';
        json << R"(            "mode" : ")"
             << execution_model_name(entry_point.execution_model) << R"(")"
             << '\n';
        json << "        }";
    }
    if (!entry_points.empty()) {
        json << "\n    ";
    }
    json << "]";

    bool first_property = false;
    write_resource_array(json, "ubos", ubos, first_property);
    write_resource_array(json, "ssbos", ssbos, first_property);
    write_resource_array(json, "textures", textures, first_property);
    write_resource_array(
        json,
        "separate_images",
        separate_images,
        first_property
    );
    write_resource_array(json, "images", images, first_property);
    write_resource_array(
        json,
        "separate_samplers",
        separate_samplers,
        first_property
    );
    json << "\n}\n";
    return json.str();
}

void unset_descriptor_layouts_for_all_resources(
    spirv_cross::CompilerGLSL& compiler
) {
    const auto resources = compiler.get_shader_resources();
    unset_descriptor_layouts(compiler, resources.uniform_buffers);
    unset_descriptor_layouts(compiler, resources.storage_buffers);
    unset_descriptor_layouts(compiler, resources.sampled_images);
    unset_descriptor_layouts(compiler, resources.separate_images);
    unset_descriptor_layouts(compiler, resources.storage_images);
    unset_descriptor_layouts(compiler, resources.separate_samplers);
}

std::string compile_opengl_glsl(spirv_cross::CompilerGLSL& compiler) {
    unset_descriptor_layouts_for_all_resources(compiler);

    spirv_cross::CompilerGLSL::Options options;
    options.version = 450;
    options.es = false;
    options.vulkan_semantics = false;
    compiler.set_common_options(options);
    return compiler.compile();
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    CLI::App app {"Generate shader backend artifacts"};
    configure_options(app, options);
    CLI11_PARSE(app, argc, argv);

    try {
        auto spirv = read_spirv(options.input);
        spirv_cross::CompilerGLSL compiler(std::move(spirv));

        auto reflection = make_reflection_json(compiler);
        auto glsl = compile_opengl_glsl(compiler);

        write_text(options.glsl, glsl);
        write_text(options.reflect, reflection);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fei-shadergen: " << e.what() << '\n';
        return 1;
    }
}
