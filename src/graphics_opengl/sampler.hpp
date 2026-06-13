#pragma once
#include "graphics/sampler.hpp"
#include "graphics_opengl/utils.hpp"

namespace fei {

class SamplerOpenGL : public Sampler {
  private:
    SamplerDescription m_desc;
    GLuint m_sampler {0};

  public:
    SamplerOpenGL(const SamplerDescription& desc);
    ~SamplerOpenGL() override;
    GLuint id() const { return m_sampler; }
};

} // namespace fei
