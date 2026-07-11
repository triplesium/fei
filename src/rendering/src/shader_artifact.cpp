#include "shader_artifact.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <spirv_cross/spirv_cross_c.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fei {
namespace {

struct ReflectedResource {
    std::string name;
    std::string backend_name;
    std::vector<std::string> backend_names;
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

struct StorageBufferBlockDeclaration {
    size_t block_name_position;
    size_t block_name_length;
    std::string block_name;
    std::string instance_name;
};

struct SourceReplacement {
    size_t position;
    size_t length;
    std::string value;
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
        if (spvc_context_create(&m_context) != SPVC_SUCCESS) {
            throw std::runtime_error("SPIRV-Cross failed to create context");
        }

        spvc_parsed_ir ir = nullptr;
        check(
            spvc_context_parse_spirv(
                m_context,
                reinterpret_cast<const SpvId*>(spirv.data()),
                spirv.size(),
                &ir
            ),
            "SPIRV-Cross failed to parse SPIR-V"
        );
        check(
            spvc_context_create_compiler(
                m_context,
                SPVC_BACKEND_GLSL,
                ir,
                SPVC_CAPTURE_MODE_TAKE_OWNERSHIP,
                &m_compiler
            ),
            "SPIRV-Cross failed to create GLSL compiler"
        );
    }

    SpirvCrossCompiler(const SpirvCrossCompiler&) = delete;
    SpirvCrossCompiler& operator=(const SpirvCrossCompiler&) = delete;

    ~SpirvCrossCompiler() {
        if (m_context != nullptr) {
            spvc_context_destroy(m_context);
        }
    }

    [[nodiscard]] spvc_compiler compiler() const { return m_compiler; }

    void check(spvc_result result, std::string_view action) const {
        if (result != SPVC_SUCCESS) {
            throw std::runtime_error(spvc_error_message(m_context, action));
        }
    }

  private:
    spvc_context m_context = nullptr;
    spvc_compiler m_compiler = nullptr;
};

std::vector<uint32_t> spirv_words(const std::vector<std::byte>& spirv) {
    if (spirv.size() % sizeof(uint32_t) != 0) {
        throw std::runtime_error("invalid SPIR-V byte size");
    }

    std::vector<uint32_t> words(spirv.size() / sizeof(uint32_t));
    if (!spirv.empty()) {
        std::memcpy(words.data(), spirv.data(), spirv.size());
    }
    return words;
}

std::string c_string(const char* value) {
    return value == nullptr ? std::string {} : std::string {value};
}

std::string fallback_name(uint32_t id) {
    return "_" + std::to_string(id);
}

std::string resource_key(uint32_t set, uint32_t binding) {
    return std::to_string(set) + ":" + std::to_string(binding);
}

void insert_unique(std::vector<std::string>& values, std::string value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(std::move(value));
    }
}

LogicalResourceNames make_logical_resource_names(
    const std::vector<ShaderArtifactLogicalResourceName>& resources
) {
    LogicalResourceNames names;
    for (const auto& resource : resources) {
        if (resource.name.empty()) {
            continue;
        }
        names[resource_key(resource.set, resource.binding)] = resource.name;
    }
    return names;
}

uint32_t
decoration_or_zero(spvc_compiler compiler, SpvId id, SpvDecoration decoration) {
    if (spvc_compiler_has_decoration(compiler, id, decoration) == SPVC_FALSE) {
        return 0;
    }
    return spvc_compiler_get_decoration(compiler, id, decoration);
}

bool is_identifier_char(char value) {
    return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_';
}

size_t skip_whitespace(std::string_view source, size_t offset) {
    while (offset < source.size() &&
           std::isspace(static_cast<unsigned char>(source[offset])) != 0) {
        ++offset;
    }
    return offset;
}

std::string
read_identifier(std::string_view source, size_t offset, size_t& end) {
    end = offset;
    if (offset >= source.size() || !is_identifier_char(source[offset]) ||
        std::isdigit(static_cast<unsigned char>(source[offset])) != 0) {
        return {};
    }

    while (end < source.size() && is_identifier_char(source[end])) {
        ++end;
    }
    return std::string(source.substr(offset, end - offset));
}

std::vector<StorageBufferBlockDeclaration>
find_storage_buffer_blocks(std::string_view source) {
    std::vector<StorageBufferBlockDeclaration> declarations;
    size_t offset = 0;
    while ((offset = source.find("buffer", offset)) != std::string_view::npos) {
        const auto previous_is_identifier =
            offset > 0 && is_identifier_char(source[offset - 1]);
        const auto after_keyword = offset + std::string_view("buffer").size();
        const auto next_is_identifier =
            after_keyword < source.size() &&
            is_identifier_char(source[after_keyword]);
        if (previous_is_identifier || next_is_identifier) {
            offset = after_keyword;
            continue;
        }

        auto block_name_position = skip_whitespace(source, after_keyword);
        size_t block_name_end = block_name_position;
        auto block_name =
            read_identifier(source, block_name_position, block_name_end);
        if (block_name.empty()) {
            offset = after_keyword;
            continue;
        }

        auto body_start = skip_whitespace(source, block_name_end);
        if (body_start >= source.size() || source[body_start] != '{') {
            offset = block_name_end;
            continue;
        }

        size_t body_end = std::string_view::npos;
        int depth = 0;
        for (size_t cursor = body_start; cursor < source.size(); ++cursor) {
            if (source[cursor] == '{') {
                ++depth;
            } else if (source[cursor] == '}') {
                --depth;
                if (depth == 0) {
                    body_end = cursor;
                    break;
                }
            }
        }
        if (body_end == std::string_view::npos) {
            offset = body_start + 1;
            continue;
        }

        auto instance_name_position = skip_whitespace(source, body_end + 1);
        size_t instance_name_end = instance_name_position;
        auto instance_name =
            read_identifier(source, instance_name_position, instance_name_end);
        declarations.push_back(
            StorageBufferBlockDeclaration {
                .block_name_position = block_name_position,
                .block_name_length = block_name_end - block_name_position,
                .block_name = std::move(block_name),
                .instance_name = std::move(instance_name),
            }
        );
        offset = instance_name_end;
    }
    return declarations;
}

bool resource_name_matches(
    std::string_view name,
    const ShaderResourceBinding& resource
) {
    if (name.empty()) {
        return false;
    }
    if (name == resource.name || name == resource.backend_name) {
        return true;
    }
    return std::any_of(
        resource.backend_names.begin(),
        resource.backend_names.end(),
        [name](const std::string& backend_name) {
            return name == backend_name;
        }
    );
}

void rewrite_opengl_storage_buffer_blocks(
    std::string& source,
    std::vector<ShaderResourceBinding>& bindings
) {
    auto declarations = find_storage_buffer_blocks(source);
    std::vector<bool> used(declarations.size(), false);
    std::vector<SourceReplacement> replacements;

    for (auto& binding : bindings) {
        if (binding.kind != ResourceKind::StorageBufferReadWrite) {
            continue;
        }

        auto declaration_index = declarations.size();
        for (size_t i = 0; i < declarations.size(); ++i) {
            if (!used[i] &&
                resource_name_matches(declarations[i].instance_name, binding)) {
                declaration_index = i;
                break;
            }
        }
        if (declaration_index == declarations.size()) {
            for (size_t i = 0; i < declarations.size(); ++i) {
                if (!used[i] && resource_name_matches(
                                    declarations[i].block_name,
                                    binding
                                )) {
                    declaration_index = i;
                    break;
                }
            }
        }
        if (declaration_index == declarations.size()) {
            continue;
        }

        used[declaration_index] = true;
        auto block_name = binding.name + "_block";
        replacements.push_back(
            SourceReplacement {
                .position = declarations[declaration_index].block_name_position,
                .length = declarations[declaration_index].block_name_length,
                .value = block_name,
            }
        );
        binding.backend_name = block_name;
        binding.backend_names = {std::move(block_name)};
    }

    std::sort(
        replacements.begin(),
        replacements.end(),
        [](const SourceReplacement& lhs, const SourceReplacement& rhs) {
            return lhs.position > rhs.position;
        }
    );
    for (const auto& replacement : replacements) {
        source.replace(
            replacement.position,
            replacement.length,
            replacement.value
        );
    }
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

uint32_t shader_resource_array_size(const ReflectedResource& resource) {
    if (resource.array.empty() || resource.array.front() == 0) {
        return 1;
    }
    return resource.array.front();
}

void append_resource_bindings(
    std::vector<ShaderResourceBinding>& bindings,
    const std::vector<ReflectedResource>& resources,
    ResourceKind kind
) {
    for (const auto& resource : resources) {
        bindings.push_back(
            ShaderResourceBinding {
                .name = resource.name,
                .backend_name = resource.backend_name,
                .backend_names = resource.backend_names,
                .kind = kind,
                .set = resource.set,
                .binding = resource.binding,
                .array_size = shader_resource_array_size(resource),
            }
        );
    }
}

std::vector<ShaderResourceBinding> make_resource_bindings(
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

    std::vector<ShaderResourceBinding> bindings;
    append_resource_bindings(
        bindings,
        reflect_resources(
            cross,
            shader_resources,
            SPVC_RESOURCE_TYPE_UNIFORM_BUFFER,
            logical_names,
            opengl_names.backend_names
        ),
        ResourceKind::UniformBuffer
    );
    append_resource_bindings(
        bindings,
        reflect_resources(
            cross,
            shader_resources,
            SPVC_RESOURCE_TYPE_STORAGE_BUFFER,
            logical_names,
            opengl_names.backend_names
        ),
        ResourceKind::StorageBufferReadWrite
    );
    append_resource_bindings(
        bindings,
        reflect_resources(
            cross,
            shader_resources,
            SPVC_RESOURCE_TYPE_SAMPLED_IMAGE,
            logical_names,
            opengl_names.backend_names,
            opengl_names.skipped_resource_ids
        ),
        ResourceKind::TextureReadOnly
    );
    append_resource_bindings(
        bindings,
        reflect_resources(
            cross,
            shader_resources,
            SPVC_RESOURCE_TYPE_SEPARATE_IMAGE,
            logical_names,
            opengl_names.backend_names
        ),
        ResourceKind::TextureReadOnly
    );
    append_resource_bindings(
        bindings,
        reflect_resources(
            cross,
            shader_resources,
            SPVC_RESOURCE_TYPE_STORAGE_IMAGE,
            logical_names,
            opengl_names.backend_names
        ),
        ResourceKind::TextureReadWrite
    );
    append_resource_bindings(
        bindings,
        reflect_resources(
            cross,
            shader_resources,
            SPVC_RESOURCE_TYPE_SEPARATE_SAMPLERS,
            logical_names,
            opengl_names.backend_names
        ),
        ResourceKind::Sampler
    );
    return bindings;
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

ShaderArtifactGenerationOutput
generate_shader_artifacts(const ShaderArtifactGenerationInput& input) {
    auto logical_names =
        make_logical_resource_names(input.logical_resource_names);
    SpirvCrossCompiler compiler(spirv_words(input.spirv));
    auto opengl_names = prepare_opengl_resource_names(compiler);
    auto resources =
        make_resource_bindings(compiler, logical_names, opengl_names);
    auto glsl = compile_opengl_glsl(compiler);
    rewrite_opengl_storage_buffer_blocks(glsl, resources);

    return ShaderArtifactGenerationOutput {
        .opengl_source = std::move(glsl),
        .resources = std::move(resources),
    };
}

std::string shader_artifact_cache_identity() {
    constexpr std::string_view artifact_version = "fei-shader-artifact-v1";
    auto* spirv_cross_version = spvc_get_commit_revision_and_timestamp();
    if (spirv_cross_version == nullptr) {
        return std::string(artifact_version);
    }
    return std::string(artifact_version) + '|' + spirv_cross_version;
}

} // namespace fei
