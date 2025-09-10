#pragma once
#include "graphics/enums.hpp"

#include <string>

namespace fei {

struct ShaderDescription {
    ShaderStage stage;
    std::string source;

    ShaderDescription(ShaderStage stage, const std::string& source) :
        stage(stage), source(source) {}
};

class ShaderModule {
  private:
    ShaderStage m_stage;

  public:
    ShaderModule(const ShaderDescription& desc) : m_stage(desc.stage) {}
    virtual ~ShaderModule() = default;

    ShaderStage stage() const { return m_stage; }
};

} // namespace fei
