#pragma once
#include "graphics/opengl/utils.hpp"
#include "graphics/sampler.hpp"

namespace fei {

class SamplerOpenGL : public Sampler {
  private:
    SamplerDescription m_desc;
    GLuint m_sampler {0};

  public:
    SamplerOpenGL(const SamplerDescription& desc);
    virtual ~SamplerOpenGL();
    GLuint id() const { return m_sampler; }
};

} // namespace fei
