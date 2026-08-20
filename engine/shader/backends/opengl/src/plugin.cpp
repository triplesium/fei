#include "shader_opengl/plugin.hpp"

#include "app/app.hpp"
#include "artifact.hpp"

#include <stdexcept>
#include <utility>

namespace fei {
namespace {

class OpenGLArtifactGenerator final : public ShaderArtifactGenerator {
  public:
    [[nodiscard]] std::string cache_identity() const override {
        return opengl_shader_artifact_cache_identity();
    }

    Result<ShaderArtifactGenerationOutput, ShaderCompileError>
    generate(ShaderArtifactGenerationInput input) override {
        try {
            return generate_opengl_shader_artifacts(input);
        } catch (const std::exception& error) {
            return failure(
                ShaderCompileError {
                    .message =
                        std::string("OpenGL shader generation failed: ") +
                        error.what(),
                }
            );
        }
    }
};

class OpenGLShaderCompilerProvider final : public ShaderCompilerProvider {
  public:
    [[nodiscard]] std::unique_ptr<ShaderCompiler> create() const override {
        return std::make_unique<OpenGLShaderCompiler>();
    }
};

} // namespace

OpenGLShaderCompiler::OpenGLShaderCompiler(ShaderCompileTarget target) :
    m_generator(std::make_unique<OpenGLArtifactGenerator>()),
    m_compiler(target, m_generator.get()) {}

OpenGLShaderCompiler::~OpenGLShaderCompiler() = default;

std::string OpenGLShaderCompiler::cache_identity() const {
    return m_compiler.cache_identity();
}

Result<ShaderCompileOutput, ShaderCompileError>
OpenGLShaderCompiler::compile(ShaderCompileRequest request) {
    return m_compiler.compile(std::move(request));
}

void OpenGLShaderPlugin::setup(App& app) {
    if (app.has_resource<ShaderCompilerProvider>()) {
        throw std::runtime_error(
            "A shader compiler provider is already installed"
        );
    }
    app.add_resource_as<ShaderCompilerProvider>(
        OpenGLShaderCompilerProvider {}
    );
}

} // namespace fei
