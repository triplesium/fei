#include "graphics_opengl/pipeline.hpp"

#include "base/log.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/resource.hpp"
#include "graphics_opengl/resource.hpp"
#include "graphics_opengl/shader_module.hpp"
#include "graphics_opengl/utils.hpp"

#include <algorithm>
#include <numeric>
#include <string_view>
#include <vector>

namespace fei {

namespace {

struct ResourceBindingLimits {
    GLuint uniform_bindings;
    GLuint storage_bindings;
    GLuint texture_units;
    GLuint image_units;
};

GLuint get_gl_limit(GLenum name) {
    GLint value = 0;
    glGetIntegerv(name, &value);
    opengl_check_error();
    return static_cast<GLuint>(value);
}

ResourceBindingLimits get_resource_binding_limits() {
    return ResourceBindingLimits {
        .uniform_bindings = get_gl_limit(GL_MAX_UNIFORM_BUFFER_BINDINGS),
        .storage_bindings = get_gl_limit(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS),
        .texture_units = get_gl_limit(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS),
        .image_units = get_gl_limit(GL_MAX_IMAGE_UNITS),
    };
}

void validate_resource_binding(
    GLuint binding,
    GLuint limit,
    std::string_view kind,
    const std::string& name
) {
    if (binding >= limit) {
        fei::fatal(
            "OpenGL {} binding {} for resource '{}' exceeds device limit {}",
            kind,
            binding,
            name,
            limit
        );
    }
}

bool resource_kind_matches(ResourceKind shader_kind, ResourceKind layout_kind) {
    if (shader_kind == layout_kind) {
        return true;
    }
    if (shader_kind == ResourceKind::StorageBufferReadWrite) {
        return layout_kind == ResourceKind::StorageBufferReadOnly ||
               layout_kind == ResourceKind::StorageBufferReadWrite;
    }
    return false;
}

std::string indexed_resource_name(const std::string& name, uint32 index) {
    if (index == 0) {
        return name;
    }
    return name + "[" + std::to_string(index) + "]";
}

bool resource_name_matches(
    const std::string& shader_name,
    const std::string& layout_name,
    uint32 array_index
) {
    if (array_index == 0 && layout_name == shader_name) {
        return true;
    }
    return layout_name == shader_name + "[" + std::to_string(array_index) + "]";
}

} // namespace

PipelineOpenGL::PipelineOpenGL(const RenderPipelineDescription& desc) :
    m_shaders(desc.shader_program.shaders),
    m_vertex_layouts(desc.shader_program.vertex_layouts),
    m_blend_state(desc.blend_state),
    m_depth_stencil_state(desc.depth_stencil_state),
    m_rasterizer_state(desc.rasterizer_state),
    m_render_primitive(desc.render_primitive),
    m_resource_layouts(desc.resource_layouts) {
    validate_shader_resource_layouts();
}

PipelineOpenGL::PipelineOpenGL(const ComputePipelineDescription& desc) :
    m_shaders({desc.shader}), m_resource_layouts(desc.resource_layouts) {
    validate_shader_resource_layouts();
}

void PipelineOpenGL::create_gl_resource() const {
    m_program = glCreateProgram();
    for (const auto& shader : m_shaders) {
        auto shader_gl = std::static_pointer_cast<ShaderOpenGL>(shader);
        shader_gl->ensure_created();
        glAttachShader(m_program, shader_gl->id());
        opengl_check_error();
    }

    glLinkProgram(m_program);
    opengl_check_error();

    GLint link_status;
    glGetProgramiv(m_program, GL_LINK_STATUS, &link_status);
    opengl_check_error();
    if (link_status == GL_FALSE) {
        GLint info_log_length;
        glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &info_log_length);
        opengl_check_error();
        std::string info_log(info_log_length, ' ');
        glGetProgramInfoLog(
            m_program,
            info_log_length,
            nullptr,
            info_log.data()
        );
        fei::fatal("Failed to link OpenGL program: {}", info_log);
    }

    process_resource_layouts();
}

void PipelineOpenGL::destroy_gl_resource() {
    if (m_program != 0) {
        glDeleteProgram(m_program);
        opengl_check_error();
        m_program = 0;
        m_resource_bindings.clear();
        m_memory_barriers = 0;
    }
}

void PipelineOpenGL::validate_shader_resource_layouts() const {
    for (const auto& shader : m_shaders) {
        for (const auto& resource : shader->resources()) {
            if (resource.set >= m_resource_layouts.size()) {
                fei::fatal(
                    "Shader '{}' resource '{}' uses set {}, binding {}, but "
                    "pipeline has only {} resource set layout(s)",
                    shader->path(),
                    resource.name,
                    resource.set,
                    resource.binding,
                    m_resource_layouts.size()
                );
            }

            auto layout = std::static_pointer_cast<ResourceLayoutOpenGL>(
                m_resource_layouts[resource.set]
            );
            const auto& elements = layout->elements();
            for (uint32 array_index = 0; array_index < resource.array_size;
                 ++array_index) {
                uint32 binding = resource.binding + array_index;
                auto it = std::find_if(
                    elements.begin(),
                    elements.end(),
                    [&](const ResourceLayoutElementDescription& element) {
                        return element.binding == binding;
                    }
                );
                if (it == elements.end()) {
                    fei::fatal(
                        "Shader '{}' resource '{}' uses set {}, binding {}, "
                        "but pipeline resource set {} has no matching binding",
                        shader->path(),
                        indexed_resource_name(resource.name, array_index),
                        resource.set,
                        binding,
                        resource.set
                    );
                }

                if (!resource_name_matches(
                        resource.name,
                        it->name,
                        array_index
                    )) {
                    fei::fatal(
                        "Shader '{}' resource set {}, binding {} is named "
                        "'{}', but pipeline layout names it '{}'",
                        shader->path(),
                        resource.set,
                        binding,
                        indexed_resource_name(resource.name, array_index),
                        it->name
                    );
                }

                if (!resource_kind_matches(resource.kind, it->kind)) {
                    fei::fatal(
                        "Shader '{}' resource '{}', set {}, binding {} is {}, "
                        "but pipeline layout declares {}",
                        shader->path(),
                        indexed_resource_name(resource.name, array_index),
                        resource.set,
                        binding,
                        resource_kind_name(resource.kind),
                        resource_kind_name(it->kind)
                    );
                }
            }
        }
    }
}

void PipelineOpenGL::process_resource_layouts() const {
    m_resource_bindings.clear();
    m_resource_bindings.resize(m_resource_layouts.size());
    m_memory_barriers = 0;
    GLuint next_uniform_binding = 0;
    GLuint next_storage_binding = 0;
    GLuint next_texture_unit = 0;
    GLuint next_image_unit = 0;
    const auto limits = get_resource_binding_limits();

    for (size_t slot = 0; slot < m_resource_layouts.size(); ++slot) {
        auto layout = std::static_pointer_cast<ResourceLayoutOpenGL>(
            m_resource_layouts[slot]
        );
        const auto& elements = layout->elements();
        m_resource_bindings[slot].resize(elements.size(), EmptyBinding {});
        std::vector<GLuint> sampler_tracked_texture_units;
        std::vector<size_t> element_indices(elements.size());
        std::iota(element_indices.begin(), element_indices.end(), 0);
        std::stable_sort(
            element_indices.begin(),
            element_indices.end(),
            [&elements](size_t lhs, size_t rhs) {
                return elements[lhs].binding < elements[rhs].binding;
            }
        );

        for (auto i : element_indices) {
            const auto& element = elements[i];
            switch (element.kind) {
                case ResourceKind::UniformBuffer: {
                    auto index =
                        glGetUniformBlockIndex(m_program, element.name.c_str());
                    opengl_check_error();
                    if (index == GL_INVALID_INDEX) {
                        continue;
                    }
                    validate_resource_binding(
                        next_uniform_binding,
                        limits.uniform_bindings,
                        "uniform buffer",
                        element.name
                    );
                    auto binding = next_uniform_binding++;
                    glUniformBlockBinding(m_program, index, binding);
                    opengl_check_error();
                    m_resource_bindings[slot][i] = UniformBinding {
                        .block_index = index,
                        .binding = binding,
                    };
                    break;
                }
                case ResourceKind::TextureReadOnly: {
                    auto location =
                        glGetUniformLocation(m_program, element.name.c_str());
                    opengl_check_error();
                    if (location == -1) {
                        continue;
                    }
                    validate_resource_binding(
                        next_texture_unit,
                        limits.texture_units,
                        "sampled texture",
                        element.name
                    );
                    auto unit = next_texture_unit++;
                    glProgramUniform1i(m_program, location, to_gl_int(unit));
                    opengl_check_error();
                    m_resource_bindings[slot][i] = TextureBinding {
                        .unit = unit,
                        .location = location,
                    };
                    sampler_tracked_texture_units.push_back(unit);
                    break;
                }
                case ResourceKind::TextureReadWrite: {
                    auto location =
                        glGetUniformLocation(m_program, element.name.c_str());
                    opengl_check_error();
                    if (location == -1) {
                        continue;
                    }
                    validate_resource_binding(
                        next_image_unit,
                        limits.image_units,
                        "storage image",
                        element.name
                    );
                    auto unit = next_image_unit++;
                    glProgramUniform1i(m_program, location, to_gl_int(unit));
                    opengl_check_error();
                    m_resource_bindings[slot][i] = TextureBinding {
                        .unit = unit,
                        .location = location,
                    };
                    m_memory_barriers |= GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
                    m_memory_barriers |= GL_TEXTURE_FETCH_BARRIER_BIT;
                    break;
                }
                case ResourceKind::StorageBufferReadOnly:
                case ResourceKind::StorageBufferReadWrite: {
                    auto index = glGetProgramResourceIndex(
                        m_program,
                        GL_SHADER_STORAGE_BLOCK,
                        element.name.c_str()
                    );
                    opengl_check_error();
                    if (index == GL_INVALID_INDEX) {
                        continue;
                    }
                    validate_resource_binding(
                        next_storage_binding,
                        limits.storage_bindings,
                        "storage buffer",
                        element.name
                    );
                    auto binding = next_storage_binding++;
                    glShaderStorageBlockBinding(m_program, index, binding);
                    opengl_check_error();
                    m_resource_bindings[slot][i] = ShaderStorageBinding {
                        .block_index = index,
                        .binding = binding,
                    };
                    m_memory_barriers |= GL_SHADER_STORAGE_BARRIER_BIT;
                    break;
                }
                case ResourceKind::Sampler: {
                    m_resource_bindings[slot][i] = SamplerBinding {
                        .units = sampler_tracked_texture_units,
                    };
                    sampler_tracked_texture_units.clear();
                    break;
                }
                default:
                    fei::fatal(
                        "ResourceKind {} not supported in PipelineOpenGL",
                        static_cast<uint32>(element.kind)
                    );
            }
        }
    }
}

} // namespace fei
