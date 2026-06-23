#pragma once
#include "graphics/shader_module.hpp"
#include "graphics_opengl/deferred_resource.hpp"
#include "graphics_opengl/utils.hpp"

#include <string>

namespace fei {

class ShaderOpenGL : public ShaderModule, public DeferredResourceOpenGL {
  private:
    mutable GLuint m_shader {0};
    GLenum m_stage;
    std::string m_source;

  public:
    explicit ShaderOpenGL(const ShaderDescription& desc);

    GLuint id() const { return m_shader; }

  private:
    void create_gl_resource() const override;
    void destroy_gl_resource() override;
};

} // namespace fei
