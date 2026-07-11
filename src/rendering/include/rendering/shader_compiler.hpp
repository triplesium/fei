#pragma once
#include "base/result.hpp"
#include "graphics/enums.hpp"
#include "graphics/shader_defs.hpp"
#include "graphics/shader_module.hpp"
#include "rendering/shader.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fei {

class ShaderArtifactCache;

struct ShaderCompileRequest {
    std::filesystem::path source_path;
    std::filesystem::path source_root;
    std::vector<std::filesystem::path> search_roots;
    std::filesystem::path logical_path;
    std::string source;
    ShaderStages stage {ShaderStages::None};
    std::string entry {"main"};
    ShaderDefs defs;
};

struct ShaderDependencySnapshot {
    std::filesystem::path path;
    std::string source;
};

struct ShaderCompileOutput {
    ShaderDescription description;
    std::vector<std::filesystem::path> dependencies;
    std::vector<ShaderDependencySnapshot> dependency_snapshots;
};

struct ShaderCompileError {
    std::string message;
    std::string diagnostics;
};

struct RuntimeShaderCompilerConfig {
    std::filesystem::path source_root;
    ShaderSourceRegistry shader_sources;
    std::filesystem::path cache_root;
};

struct ShaderVariantCompileOutput {
    ShaderDescription description;
    std::vector<std::filesystem::path> dependencies;
};

class ShaderCompiler {
  public:
    virtual ~ShaderCompiler() = default;
    [[nodiscard]] virtual std::string cache_identity() const { return {}; }
    virtual Result<ShaderCompileOutput, ShaderCompileError>
    compile(ShaderCompileRequest request) = 0;
};

class ShaderVariantCompiler {
  private:
    ShaderCompiler* m_compiler;
    RuntimeShaderCompilerConfig m_config;
    std::shared_ptr<ShaderArtifactCache> m_artifact_cache;

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

    Result<ShaderVariantCompileOutput, ShaderCompileError>
    compile_with_dependencies(
        std::filesystem::path logical_path,
        ShaderStages stage,
        std::string entry,
        ShaderDefs defs
    );

    Result<ShaderVariantCompileOutput, ShaderCompileError>
    compile_with_dependencies(
        std::filesystem::path logical_path,
        std::string source,
        ShaderStages stage,
        std::string entry,
        ShaderDefs defs
    );

    Result<ShaderDescription, ShaderCompileError>
    compile(std::filesystem::path logical_path, ShaderDefs defs);

    Result<ShaderDescription, ShaderCompileError> compile(
        std::filesystem::path logical_path,
        ShaderStages stage,
        std::string entry,
        ShaderDefs defs
    );
};

#ifdef FEI_HAS_SLANG_SDK
class SlangLibraryShaderCompiler final : public ShaderCompiler {
  public:
    [[nodiscard]] std::string cache_identity() const override;

    Result<ShaderCompileOutput, ShaderCompileError>
    compile(ShaderCompileRequest request) override;
};
#endif

} // namespace fei
