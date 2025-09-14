#pragma once
#include "graphics/enums.hpp"

#include <string>

namespace fei {

struct ShaderDescription {
    ShaderStages stage;
    std::string source;
};

class ShaderModule {
  private:
    ShaderStages m_stage;

  public:
    ShaderModule(const ShaderDescription& desc) : m_stage(desc.stage) {}
    virtual ~ShaderModule() = default;

    ShaderStages stage() const { return m_stage; }
};

} // namespace fei
