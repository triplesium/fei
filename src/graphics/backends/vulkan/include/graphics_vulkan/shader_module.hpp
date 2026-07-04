#pragma once
#include "graphics/shader_module.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace fei {

class ShaderVulkan : public ShaderModule {
  private:
    std::vector<std::byte> m_spirv;

  public:
    explicit ShaderVulkan(const ShaderDescription& desc);
    ~ShaderVulkan() override = default;

    std::span<const std::byte> spirv() const { return m_spirv; }
};

} // namespace fei
