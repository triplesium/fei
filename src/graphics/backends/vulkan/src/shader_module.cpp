#include "graphics_vulkan/shader_module.hpp"

#include "base/log.hpp"

namespace fei {

ShaderVulkan::ShaderVulkan(const ShaderDescription& desc) :
    ShaderModule(desc), m_spirv(desc.spirv) {
    if (m_spirv.empty()) {
        fatal(
            "Vulkan shader '{}' has no SPIR-V. Build the shader owner target.",
            desc.path
        );
    }
}

} // namespace fei
