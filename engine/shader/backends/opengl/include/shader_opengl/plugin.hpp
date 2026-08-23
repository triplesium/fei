#pragma once
#include "app/plugin.hpp"
#include "shader/compiler.hpp"

namespace ets {

class OpenGLShaderCompiler final : public ShaderCompiler {
  private:
    std::unique_ptr<ShaderArtifactGenerator> m_generator;
    SlangLibraryShaderCompiler m_compiler;

  public:
    explicit OpenGLShaderCompiler(
        ShaderCompileTarget target = ShaderCompileTarget::OpenGL
    );
    ~OpenGLShaderCompiler() override;

    [[nodiscard]] std::string cache_identity() const override;
    Result<ShaderCompileOutput, ShaderCompileError>
    compile(ShaderCompileRequest request) override;
};

ETS_REFLECT(Plugin)
class OpenGLShaderPlugin final : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace ets
