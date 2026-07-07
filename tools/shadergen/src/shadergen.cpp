#include "shadergen/shadergen.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spirv_cross/spirv_cross_c.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fei::shadergen {
namespace {

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

std::string spvc_error_message(spvc_context context, std::string_view action) {
    std::string message(action);
    if (context != nullptr) {
        if (const char* error = spvc_context_get_last_error_string(context);
            error != nullptr && error[0] != '\0') {
            message += ": ";
            message += error;
        }
    }
    return message;
}

class SpirvCrossCompiler {
  public:
    explicit SpirvCrossCompiler(const std::vector<uint32_t>& spirv) {
        if (spvc_context_create(&context_) != SPVC_SUCCESS) {
            throw std::runtime_error("SPIRV-Cross failed to create context");
        }

        spvc_parsed_ir ir = nullptr;
        check(
            spvc_context_parse_spirv(
                context_,
                reinterpret_cast<const SpvId*>(spirv.data()),
                spirv.size(),
                &ir
            ),
            "SPIRV-Cross failed to parse SPIR-V"
        );
        check(
            spvc_context_create_compiler(
                context_,
                SPVC_BACKEND_GLSL,
                ir,
                SPVC_CAPTURE_MODE_TAKE_OWNERSHIP,
                &compiler_
            ),
            "SPIRV-Cross failed to create GLSL compiler"
        );
    }

    SpirvCrossCompiler(const SpirvCrossCompiler&) = delete;
    SpirvCrossCompiler& operator=(const SpirvCrossCompiler&) = delete;

    ~SpirvCrossCompiler() {
        if (context_ != nullptr) {
            spvc_context_destroy(context_);
        }
    }

    [[nodiscard]] spvc_compiler compiler() const { return compiler_; }

    void check(spvc_result result, std::string_view action) const {
        if (result != SPVC_SUCCESS) {
            throw std::runtime_error(spvc_error_message(context_, action));
        }
    }

  private:
    spvc_context context_ = nullptr;
    spvc_compiler compiler_ = nullptr;
};

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

std::string c_string(const char* value) {
    return value == nullptr ? std::string {} : std::string {value};
}

std::string fallback_name(uint32_t id) {
    return "_" + std::to_string(id);
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

std::string execution_model_name(SpvExecutionModel model) {
    switch (model) {
        case SpvExecutionModelVertex:
            return "vert";
        case SpvExecutionModelFragment:
            return "frag";
        case SpvExecutionModelGeometry:
            return "geom";
        case SpvExecutionModelGLCompute:
            return "comp";
        case SpvExecutionModelTessellationControl:
            return "tesc";
        case SpvExecutionModelTessellationEvaluation:
            return "tese";
        default:
            return "unknown";
    }
}

std::string image_dim_name(SpvDim dim) {
    switch (dim) {
        case SpvDim1D:
            return "1D";
        case SpvDim2D:
            return "2D";
        case SpvDim3D:
            return "3D";
        case SpvDimCube:
            return "Cube";
        case SpvDimRect:
            return "Rect";
        case SpvDimBuffer:
            return "Buffer";
        case SpvDimSubpassData:
            return "SubpassData";
        default:
            return "Unknown";
    }
}

std::string resource_type_name(
    spvc_compiler compiler,
    const spvc_reflected_resource& resource
) {
    const auto type = spvc_compiler_get_type_handle(compiler, resource.type_id);
    if (type == nullptr) {
        return "unknown";
    }

    const auto base_type = spvc_type_get_basetype(type);
    if (base_type == SPVC_BASETYPE_STRUCT) {
        auto name =
            c_string(spvc_compiler_get_name(compiler, resource.base_type_id));
        return name.empty() ? fallback_name(resource.base_type_id) :
                              std::move(name);
    }
    if (base_type == SPVC_BASETYPE_SAMPLER) {
        return "sampler";
    }

    std::string prefix;
    if (base_type == SPVC_BASETYPE_SAMPLED_IMAGE) {
        prefix = "sampler";
    } else if (base_type == SPVC_BASETYPE_IMAGE) {
        prefix = "image";
    } else {
        return "unknown";
    }

    return prefix + image_dim_name(spvc_type_get_image_dimension(type));
}

uint32_t
decoration_or_zero(spvc_compiler compiler, SpvId id, SpvDecoration decoration) {
    if (spvc_compiler_has_decoration(compiler, id, decoration) == SPVC_FALSE) {
        return 0;
    }
    return spvc_compiler_get_decoration(compiler, id, decoration);
}

std::vector<uint32_t> resource_array_dimensions(
    spvc_compiler compiler,
    const spvc_reflected_resource& resource
) {
    std::vector<uint32_t> dimensions;
    const auto type = spvc_compiler_get_type_handle(compiler, resource.type_id);
    if (type == nullptr) {
        return dimensions;
    }

    const auto count = spvc_type_get_num_array_dimensions(type);
    dimensions.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        dimensions.push_back(spvc_type_get_array_dimension(type, i));
    }
    return dimensions;
}

ReflectedResource reflect_resource(
    spvc_compiler compiler,
    const spvc_reflected_resource& resource,
    const LogicalResourceNames& logical_names,
    const BackendResourceNames& backend_names
) {
    auto spirv_name = c_string(resource.name);
    if (spirv_name.empty()) {
        spirv_name = fallback_name(resource.id);
    }

    const auto set =
        decoration_or_zero(compiler, resource.id, SpvDecorationDescriptorSet);
    const auto binding =
        decoration_or_zero(compiler, resource.id, SpvDecorationBinding);
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
        .array = resource_array_dimensions(compiler, resource),
    };
}

std::vector<ReflectedResource> reflect_resources(
    const SpirvCrossCompiler& cross,
    spvc_resources resources,
    spvc_resource_type type,
    const LogicalResourceNames& logical_names,
    const BackendResourceNames& backend_names,
    const SkippedResourceIds& skipped_resource_ids = {}
) {
    const spvc_reflected_resource* resource_list = nullptr;
    size_t resource_count = 0;
    cross.check(
        spvc_resources_get_resource_list_for_type(
            resources,
            type,
            &resource_list,
            &resource_count
        ),
        "SPIRV-Cross failed to list resources"
    );

    std::vector<ReflectedResource> reflected;
    reflected.reserve(resource_count);
    for (size_t i = 0; i < resource_count; ++i) {
        const auto& resource = resource_list[i];
        if (skipped_resource_ids.contains(resource.id) ||
            c_string(resource.name) == "fei_dummy_sampler") {
            continue;
        }
        reflected.push_back(reflect_resource(
            cross.compiler(),
            resource,
            logical_names,
            backend_names
        ));
    }
    return reflected;
}

void unset_descriptor_layouts(
    const SpirvCrossCompiler& cross,
    spvc_resources resources,
    spvc_resource_type type
) {
    const spvc_reflected_resource* resource_list = nullptr;
    size_t resource_count = 0;
    cross.check(
        spvc_resources_get_resource_list_for_type(
            resources,
            type,
            &resource_list,
            &resource_count
        ),
        "SPIRV-Cross failed to list resources"
    );

    for (size_t i = 0; i < resource_count; ++i) {
        spvc_compiler_unset_decoration(
            cross.compiler(),
            resource_list[i].id,
            SpvDecorationDescriptorSet
        );
        spvc_compiler_unset_decoration(
            cross.compiler(),
            resource_list[i].id,
            SpvDecorationBinding
        );
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
    const SpirvCrossCompiler& cross,
    const LogicalResourceNames& logical_names,
    const OpenGLResourceNames& opengl_names
) {
    spvc_resources shader_resources = nullptr;
    cross.check(
        spvc_compiler_create_shader_resources(
            cross.compiler(),
            &shader_resources
        ),
        "SPIRV-Cross failed to create shader resources"
    );

    const spvc_entry_point* entry_points = nullptr;
    size_t entry_point_count = 0;
    cross.check(
        spvc_compiler_get_entry_points(
            cross.compiler(),
            &entry_points,
            &entry_point_count
        ),
        "SPIRV-Cross failed to list entry points"
    );

    const auto ubos = reflect_resources(
        cross,
        shader_resources,
        SPVC_RESOURCE_TYPE_UNIFORM_BUFFER,
        logical_names,
        opengl_names.backend_names
    );
    const auto ssbos = reflect_resources(
        cross,
        shader_resources,
        SPVC_RESOURCE_TYPE_STORAGE_BUFFER,
        logical_names,
        opengl_names.backend_names
    );
    const auto textures = reflect_resources(
        cross,
        shader_resources,
        SPVC_RESOURCE_TYPE_SAMPLED_IMAGE,
        logical_names,
        opengl_names.backend_names,
        opengl_names.skipped_resource_ids
    );
    const auto separate_images = reflect_resources(
        cross,
        shader_resources,
        SPVC_RESOURCE_TYPE_SEPARATE_IMAGE,
        logical_names,
        opengl_names.backend_names
    );
    const auto images = reflect_resources(
        cross,
        shader_resources,
        SPVC_RESOURCE_TYPE_STORAGE_IMAGE,
        logical_names,
        opengl_names.backend_names
    );
    const auto separate_samplers = reflect_resources(
        cross,
        shader_resources,
        SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS,
        logical_names,
        opengl_names.backend_names
    );

    std::ostringstream json;
    json << "{\n";
    json << "    \"entryPoints\" : [";
    for (size_t i = 0; i < entry_point_count; ++i) {
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
    if (entry_point_count != 0) {
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
    const SpirvCrossCompiler& cross
) {
    spvc_resources resources = nullptr;
    cross.check(
        spvc_compiler_create_shader_resources(cross.compiler(), &resources),
        "SPIRV-Cross failed to create shader resources"
    );
    unset_descriptor_layouts(
        cross,
        resources,
        SPVC_RESOURCE_TYPE_UNIFORM_BUFFER
    );
    unset_descriptor_layouts(
        cross,
        resources,
        SPVC_RESOURCE_TYPE_STORAGE_BUFFER
    );
    unset_descriptor_layouts(
        cross,
        resources,
        SPVC_RESOURCE_TYPE_SAMPLED_IMAGE
    );
    unset_descriptor_layouts(
        cross,
        resources,
        SPVC_RESOURCE_TYPE_SEPARATE_IMAGE
    );
    unset_descriptor_layouts(
        cross,
        resources,
        SPVC_RESOURCE_TYPE_STORAGE_IMAGE
    );
    unset_descriptor_layouts(
        cross,
        resources,
        SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS
    );
}

OpenGLResourceNames
prepare_opengl_resource_names(const SpirvCrossCompiler& cross) {
    OpenGLResourceNames names;
    spvc_variable_id dummy_sampler = 0;
    cross.check(
        spvc_compiler_build_dummy_sampler_for_combined_images(
            cross.compiler(),
            &dummy_sampler
        ),
        "SPIRV-Cross failed to build dummy sampler"
    );
    if (dummy_sampler != 0) {
        spvc_compiler_set_name(
            cross.compiler(),
            dummy_sampler,
            "fei_dummy_sampler"
        );
        names.skipped_resource_ids.insert(dummy_sampler);
    }

    cross.check(
        spvc_compiler_build_combined_image_samplers(cross.compiler()),
        "SPIRV-Cross failed to build combined image samplers"
    );

    const spvc_combined_image_sampler* samplers = nullptr;
    size_t sampler_count = 0;
    cross.check(
        spvc_compiler_get_combined_image_samplers(
            cross.compiler(),
            &samplers,
            &sampler_count
        ),
        "SPIRV-Cross failed to list combined image samplers"
    );
    for (size_t i = 0; i < sampler_count; ++i) {
        const auto& combined = samplers[i];
        const auto set = decoration_or_zero(
            cross.compiler(),
            combined.image_id,
            SpvDecorationDescriptorSet
        );
        const auto binding = decoration_or_zero(
            cross.compiler(),
            combined.image_id,
            SpvDecorationBinding
        );
        auto backend_name = c_string(
            spvc_compiler_get_name(cross.compiler(), combined.combined_id)
        );
        if (backend_name.empty()) {
            backend_name = fallback_name(combined.combined_id);
        }
        insert_unique(
            names.backend_names[resource_key(set, binding)],
            backend_name
        );
        names.skipped_resource_ids.insert(combined.combined_id);
    }
    return names;
}

std::string compile_opengl_glsl(const SpirvCrossCompiler& cross) {
    unset_descriptor_layouts_for_all_resources(cross);

    spvc_compiler_options options = nullptr;
    cross.check(
        spvc_compiler_create_compiler_options(cross.compiler(), &options),
        "SPIRV-Cross failed to create compiler options"
    );
    cross.check(
        spvc_compiler_options_set_uint(
            options,
            SPVC_COMPILER_OPTION_GLSL_VERSION,
            450
        ),
        "SPIRV-Cross failed to set GLSL version"
    );
    cross.check(
        spvc_compiler_options_set_bool(
            options,
            SPVC_COMPILER_OPTION_GLSL_ES,
            SPVC_FALSE
        ),
        "SPIRV-Cross failed to set GLSL ES option"
    );
    cross.check(
        spvc_compiler_options_set_bool(
            options,
            SPVC_COMPILER_OPTION_GLSL_VULKAN_SEMANTICS,
            SPVC_FALSE
        ),
        "SPIRV-Cross failed to set GLSL Vulkan semantics option"
    );
    cross.check(
        spvc_compiler_install_compiler_options(cross.compiler(), options),
        "SPIRV-Cross failed to install compiler options"
    );

    const char* source = nullptr;
    cross.check(
        spvc_compiler_compile(cross.compiler(), &source),
        "SPIRV-Cross failed to compile GLSL"
    );
    return c_string(source);
}

} // namespace

void generate_shader_artifacts(const ShaderArtifactGenerationRequest& request) {
    auto spirv = read_spirv(request.spirv_path);
    auto logical_names =
        load_slang_logical_resource_names(request.slang_reflection_path);
    SpirvCrossCompiler compiler(spirv);
    auto opengl_names = prepare_opengl_resource_names(compiler);

    auto reflection =
        make_reflection_json(compiler, logical_names, opengl_names);
    auto glsl = compile_opengl_glsl(compiler);

    write_text(request.opengl_path, glsl);
    write_text(request.reflection_path, reflection);
}

} // namespace fei::shadergen
