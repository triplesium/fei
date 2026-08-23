#pragma once
#include "base/result.hpp"
#include "graphics/enums.hpp"
#include "graphics/shader_defs.hpp"
#include "graphics/shader_module.hpp"
#include "shader/shader.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ets {

class ShaderArtifactCache;

enum class ShaderCompileTarget : std::uint8_t {
    All,
    OpenGL,
    Vulkan,
    WebGpu,
};

struct ShaderCompileRequest {
    std::filesystem::path source_path;
    std::filesystem::path source_root;
    std::vector<std::filesystem::path> search_roots;
    std::filesystem::path logical_path;
    std::string source;
    ShaderStages stage {ShaderStages::None};
    std::string entry {"main"};
    ShaderDefs defs;
    std::shared_ptr<const ShaderSourceSnapshot> source_snapshot;
    ShaderCompileTarget target {ShaderCompileTarget::All};
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
    ShaderCompileTarget target {ShaderCompileTarget::All};
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

class ShaderCompilerProvider {
  public:
    virtual ~ShaderCompilerProvider() = default;
    [[nodiscard]] virtual std::unique_ptr<ShaderCompiler> create() const = 0;
};

class BoxedShaderCompiler final : public ShaderCompiler {
  private:
    std::unique_ptr<ShaderCompiler> m_compiler;

  public:
    explicit BoxedShaderCompiler(std::unique_ptr<ShaderCompiler> compiler);

    [[nodiscard]] std::string cache_identity() const override;
    Result<ShaderCompileOutput, ShaderCompileError>
    compile(ShaderCompileRequest request) override;
};

struct ShaderArtifactLogicalResourceName {
    std::string name;
    std::uint32_t set {0};
    std::uint32_t binding {0};
};

struct ShaderArtifactGenerationInput {
    std::vector<std::byte> spirv;
    std::vector<ShaderArtifactLogicalResourceName> logical_resource_names;
};

struct ShaderArtifactGenerationOutput {
    std::string source;
    std::vector<ShaderResourceBinding> resources;
};

class ShaderArtifactGenerator {
  public:
    virtual ~ShaderArtifactGenerator() = default;
    [[nodiscard]] virtual std::string cache_identity() const = 0;
    virtual Result<ShaderArtifactGenerationOutput, ShaderCompileError>
    generate(ShaderArtifactGenerationInput input) = 0;
};

class ShaderVariantCompiler {
  private:
    ShaderCompiler* m_compiler;
    RuntimeShaderCompilerConfig m_config;
    std::shared_ptr<ShaderArtifactCache> m_artifact_cache;
    std::shared_ptr<const ShaderSourceSnapshot> m_source_snapshot;

  public:
    ShaderVariantCompiler(
        ShaderCompiler& compiler,
        RuntimeShaderCompilerConfig config = {}
    );

    [[nodiscard]] const RuntimeShaderCompilerConfig& config() const {
        return m_config;
    }

    void
    set_source_snapshot(std::shared_ptr<const ShaderSourceSnapshot> snapshot) {
        m_source_snapshot = std::move(snapshot);
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

class SlangLibraryShaderCompiler final : public ShaderCompiler {
  private:
    ShaderCompileTarget m_target;
    ShaderArtifactGenerator* m_artifact_generator;

  public:
    explicit SlangLibraryShaderCompiler(
        ShaderCompileTarget target,
        ShaderArtifactGenerator* artifact_generator = nullptr
    );

    [[nodiscard]] std::string cache_identity() const override;

    Result<ShaderCompileOutput, ShaderCompileError>
    compile(ShaderCompileRequest request) override;
};

} // namespace ets
