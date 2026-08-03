#pragma once
#include "graphics/sampler.hpp"
#include "graphics_opengl/deferred_resource.hpp"
#include "graphics_opengl/utils.hpp"

namespace fei {

class SamplerOpenGL : public Sampler, public DeferredResourceOpenGL {
  private:
    SamplerDescription m_desc;
    mutable GLuint m_sampler {0};

  public:
    explicit SamplerOpenGL(const SamplerDescription& desc);
    GLuint id() const { return m_sampler; }

  private:
    void create_gl_resource() const override;
    void destroy_gl_resource() override;
};

} // namespace fei
