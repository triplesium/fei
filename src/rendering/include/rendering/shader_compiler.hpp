#pragma once
#include "base/result.hpp"
#include "graphics/enums.hpp"
#include "graphics/shader_defs.hpp"
#include "graphics/shader_module.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace fei {

enum class ShaderSourceLanguage {
    Glsl,
    Slang,
};

struct ShaderCompileTools {
    std::filesystem::path glslc;
    std::filesystem::path slangc;
    std::filesystem::path shadergen;
};

struct ShaderCompileRequest {
    ShaderSourceLanguage language {ShaderSourceLanguage::Slang};
    std::filesystem::path source_path;
    std::filesystem::path source_root;
    std::filesystem::path logical_path;
    std::filesystem::path output_root;
    std::filesystem::path dependency_path;
    ShaderStages stage {ShaderStages::None};
    std::string entry {"main"};
    ShaderDefs defs;
};

struct ShaderCompileArtifacts {
    std::filesystem::path spirv_path;
    std::filesystem::path opengl_path;
    std::filesystem::path reflection_path;
    std::filesystem::path slang_reflection_path;
    std::filesystem::path depfile_path;
};

struct ShaderCompilerInvocation {
    std::filesystem::path program;
    std::vector<std::string> args;
};

struct ShaderCompilePlan {
    ShaderCompileArtifacts artifacts;
    std::vector<ShaderCompilerInvocation> invocations;
};

struct ShaderCompileOutput {
    ShaderCompileArtifacts artifacts;
    std::vector<std::filesystem::path> dependencies;
};

struct ShaderCompileError {
    std::string message;
    std::string diagnostics;
};

struct RuntimeShaderCompilerConfig {
    std::filesystem::path source_root;
    std::filesystem::path output_root;
};

struct ShaderVariantCompileOutput {
    ShaderDescription description;
    std::vector<std::filesystem::path> dependencies;
};

using ShaderCompilerCommandRunner =
    std::function<Status<ShaderCompileError>(const ShaderCompilerInvocation&)>;

std::string shader_stage_name(ShaderStages stage);
std::string shader_def_define_value(const ShaderDefValue& value);
std::string shader_def_define_argument(const ShaderDefVal& def);

ShaderCompileArtifacts
shader_compile_artifact_paths(const ShaderCompileRequest& request);

Result<ShaderCompilePlan, ShaderCompileError> make_shader_compile_plan(
    ShaderCompileRequest request,
    const ShaderCompileTools& tools
);

Result<ShaderCompilePlan, ShaderCompileError> make_shadergen_compile_plan(
    ShaderCompileRequest request,
    const ShaderCompileTools& tools
);

class ShaderCompiler {
  public:
    virtual ~ShaderCompiler() = default;
    virtual Result<ShaderCompileOutput, ShaderCompileError>
    compile(ShaderCompileRequest request) = 0;
};

class ExternalShaderCompiler final : public ShaderCompiler {
  private:
    ShaderCompileTools m_tools;
    ShaderCompilerCommandRunner m_runner;

  public:
    ExternalShaderCompiler(
        ShaderCompileTools tools,
        ShaderCompilerCommandRunner runner
    );

    Result<ShaderCompileOutput, ShaderCompileError>
    compile(ShaderCompileRequest request) override;
};

class ShaderVariantCompiler {
  private:
    ShaderCompiler* m_compiler;
    RuntimeShaderCompilerConfig m_config;

  public:
    ShaderVariantCompiler(
        ShaderCompiler& compiler,
        RuntimeShaderCompilerConfig config = {}
    );

    [[nodiscard]] const RuntimeShaderCompilerConfig& config() const {
        return m_config;
    }

    Result<ShaderVariantCompileOutput, ShaderCompileError>
    compile_with_dependencies(
        std::filesystem::path logical_path,
        ShaderDefs defs
    );

    Result<ShaderDescription, ShaderCompileError>
    compile(std::filesystem::path logical_path, ShaderDefs defs);
};

#ifdef FEI_HAS_SLANG_SDK
class SlangLibraryShaderCompiler final : public ShaderCompiler {
  public:
    Result<ShaderCompileOutput, ShaderCompileError>
    compile(ShaderCompileRequest request) override;
};
#endif

} // namespace fei
