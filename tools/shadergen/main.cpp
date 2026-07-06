#include <algorithm>
#include <CLI/CLI.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <spirv_cross/spirv_cross.hpp>
#include <spirv_cross/spirv_glsl.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct Options {
    std::filesystem::path input;
    std::filesystem::path glsl;
    std::filesystem::path reflect;
    std::filesystem::path slang_reflect;
};

struct ReflectedResource {
    std::string name;
    std::string backend_name;
    std::vector<std::string> backend_names;
    std::string type;
    uint32_t set;
    uint32_t binding;
    std::vector<uint32_t> array;
};

using LogicalResourceNames = std::unordered_map<std::string, std::string>;
using BackendResourceNames =
    std::unordered_map<std::string, std::vector<std::string>>;
using SkippedResourceIds = std::unordered_set<uint32_t>;

struct OpenGLResourceNames {
    BackendResourceNames backend_names;
    SkippedResourceIds skipped_resource_ids;
};

void configure_options(CLI::App& app, Options& options) {
    app.add_option("--input", options.input, "Input SPIR-V file")
        ->required()
        ->check(CLI::ExistingFile);
    app.add_option("--glsl", options.glsl, "Output OpenGL GLSL file")
        ->required();
    app.add_option("--reflect", options.reflect, "Output reflection JSON file")
        ->required();
    app.add_option(
        "--slang-reflect",
        options.slang_reflect,
        "Optional Slang reflection JSON file for logical resource names"
    );
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

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open input: " + path.string());
    }

    std::ostringstream content;
    content << input.rdbuf();
    if (!input && !input.eof()) {
        throw std::runtime_error("failed to read input: " + path.string());
    }
    return content.str();
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

std::string resource_key(uint32_t set, uint32_t binding) {
    return std::to_string(set) + ":" + std::to_string(binding);
}

void insert_unique(std::vector<std::string>& values, std::string value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(std::move(value));
    }
}

LogicalResourceNames
load_slang_logical_resource_names(const std::filesystem::path& path) {
    LogicalResourceNames names;
    if (path.empty()) {
        return names;
    }

    const auto document = nlohmann::json::parse(read_text(path));
    const auto parameters = document.find("parameters");
    if (parameters == document.end() || !parameters->is_array()) {
        return names;
    }

    for (const auto& parameter : *parameters) {
        if (!parameter.is_object()) {
            continue;
        }
        const auto name = parameter.find("name");
        const auto binding = parameter.find("binding");
        if (name == parameter.end() || !name->is_string() ||
            binding == parameter.end() || !binding->is_object()) {
            continue;
        }

        const auto kind = binding->find("kind");
        const auto index = binding->find("index");
        if (kind == binding->end() || !kind->is_string() ||
            kind->get<std::string>() != "descriptorTableSlot" ||
            index == binding->end() || !index->is_number_unsigned()) {
            continue;
        }

        const uint32_t set = binding->value("space", static_cast<uint32_t>(0));
        const auto slot = index->get<uint32_t>();
        names[resource_key(set, slot)] = name->get<std::string>();
    }
    return names;
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
    const spirv_cross::Resource& resource,
    const LogicalResourceNames& logical_names,
    const BackendResourceNames& backend_names
) {
    const auto& type = compiler.get_type(resource.type_id);
    const auto spirv_name = resource.name.empty() ?
                                compiler.get_fallback_name(resource.id) :
                                resource.name;
    const auto set =
        decoration_or_zero(compiler, resource.id, spv::DecorationDescriptorSet);
    const auto binding =
        decoration_or_zero(compiler, resource.id, spv::DecorationBinding);
    const auto key = resource_key(set, binding);
    const auto logical_name = logical_names.find(key);
    const auto backend_names_for_resource = backend_names.find(key);
    auto reflected_backend_names =
        backend_names_for_resource == backend_names.end() ?
            std::vector<std::string> {spirv_name} :
            backend_names_for_resource->second;
    return ReflectedResource {
        .name = logical_name == logical_names.end() ? spirv_name :
                                                      logical_name->second,
        .backend_name = reflected_backend_names.front(),
        .backend_names = std::move(reflected_backend_names),
        .type = resource_type_name(compiler, resource),
        .set = set,
        .binding = binding,
        .array = std::vector<uint32_t>(type.array.begin(), type.array.end()),
    };
}

std::vector<ReflectedResource> reflect_resources(
    const spirv_cross::Compiler& compiler,
    const spirv_cross::SmallVector<spirv_cross::Resource>& resources,
    const LogicalResourceNames& logical_names,
    const BackendResourceNames& backend_names,
    const SkippedResourceIds& skipped_resource_ids = {}
) {
    std::vector<ReflectedResource> reflected;
    reflected.reserve(resources.size());
    for (const auto& resource : resources) {
        if (skipped_resource_ids.contains(resource.id) ||
            resource.name == "fei_dummy_sampler") {
            continue;
        }
        reflected.push_back(
            reflect_resource(compiler, resource, logical_names, backend_names)
        );
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
        if (resource.backend_names.size() > 1) {
            json << R"(            "backend_names" : [)";
            for (std::size_t j = 0; j < resource.backend_names.size(); ++j) {
                if (j > 0) {
                    json << ", ";
                }
                json << R"(")" << escape_json(resource.backend_names[j])
                     << R"(")";
            }
            json << R"(],)" << '\n';
        } else if (resource.backend_name != resource.name) {
            json << R"(            "backend_name" : ")"
                 << escape_json(resource.backend_name) << R"(",)" << '\n';
        }
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

std::string make_reflection_json(
    spirv_cross::CompilerGLSL& compiler,
    const LogicalResourceNames& logical_names,
    const OpenGLResourceNames& opengl_names
) {
    const auto shader_resources = compiler.get_shader_resources();
    const auto entry_points = compiler.get_entry_points_and_stages();

    const auto ubos = reflect_resources(
        compiler,
        shader_resources.uniform_buffers,
        logical_names,
        opengl_names.backend_names
    );
    const auto ssbos = reflect_resources(
        compiler,
        shader_resources.storage_buffers,
        logical_names,
        opengl_names.backend_names
    );
    const auto textures = reflect_resources(
        compiler,
        shader_resources.sampled_images,
        logical_names,
        opengl_names.backend_names,
        opengl_names.skipped_resource_ids
    );
    const auto separate_images = reflect_resources(
        compiler,
        shader_resources.separate_images,
        logical_names,
        opengl_names.backend_names
    );
    const auto images = reflect_resources(
        compiler,
        shader_resources.storage_images,
        logical_names,
        opengl_names.backend_names
    );
    const auto separate_samplers = reflect_resources(
        compiler,
        shader_resources.separate_samplers,
        logical_names,
        opengl_names.backend_names
    );

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

OpenGLResourceNames
prepare_opengl_resource_names(spirv_cross::CompilerGLSL& compiler) {
    OpenGLResourceNames names;
    const auto dummy_sampler =
        compiler.build_dummy_sampler_for_combined_images();
    if (dummy_sampler != 0) {
        compiler.set_name(dummy_sampler, "fei_dummy_sampler");
        names.skipped_resource_ids.insert(dummy_sampler);
    }

    compiler.build_combined_image_samplers();
    for (const auto& combined : compiler.get_combined_image_samplers()) {
        const auto set = decoration_or_zero(
            compiler,
            combined.image_id,
            spv::DecorationDescriptorSet
        );
        const auto binding = decoration_or_zero(
            compiler,
            combined.image_id,
            spv::DecorationBinding
        );
        auto backend_name = compiler.get_name(combined.combined_id);
        if (backend_name.empty()) {
            backend_name = compiler.get_fallback_name(combined.combined_id);
        }
        insert_unique(
            names.backend_names[resource_key(set, binding)],
            backend_name
        );
        names.skipped_resource_ids.insert(combined.combined_id);
    }
    return names;
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
        auto logical_names =
            load_slang_logical_resource_names(options.slang_reflect);
        spirv_cross::CompilerGLSL compiler(std::move(spirv));
        auto opengl_names = prepare_opengl_resource_names(compiler);

        auto reflection =
            make_reflection_json(compiler, logical_names, opengl_names);
        auto glsl = compile_opengl_glsl(compiler);

        write_text(options.glsl, glsl);
        write_text(options.reflect, reflection);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fei-shadergen: " << e.what() << '\n';
        return 1;
    }
}
