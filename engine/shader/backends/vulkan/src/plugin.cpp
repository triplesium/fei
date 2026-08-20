#include "shader_vulkan/plugin.hpp"

#include "app/app.hpp"
#include "shader/compiler.hpp"

#include <stdexcept>

namespace fei {
namespace {

class VulkanShaderCompilerProvider final : public ShaderCompilerProvider {
  public:
    [[nodiscard]] std::unique_ptr<ShaderCompiler> create() const override {
        return std::make_unique<SlangLibraryShaderCompiler>(
            ShaderCompileTarget::Vulkan
        );
    }
};

} // namespace

void VulkanShaderPlugin::setup(App& app) {
    if (app.has_resource<ShaderCompilerProvider>()) {
        throw std::runtime_error(
            "A shader compiler provider is already installed"
        );
    }
    app.add_resource_as<ShaderCompilerProvider>(
        VulkanShaderCompilerProvider {}
    );
}

} // namespace fei
