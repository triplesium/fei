#pragma once
#include "asset/handle.hpp"
#include "asset/io.hpp"
#include "asset/loader.hpp"
#include "asset/path.hpp"
#include "asset/server.hpp"
#include "asset/source.hpp"
#include "base/log.hpp"
#include "base/optional.hpp"
#include "graphics/enums.hpp"
#include "graphics/shader_module.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fei {

struct Shader {
    std::filesystem::path path;
    std::string source;
    std::vector<std::byte> spirv;
    ShaderStages stage;
    std::vector<ShaderResourceBinding> resources;

    ShaderDescription description() const {
        return ShaderDescription {
            .stage = stage,
            .source = source,
            .spirv = spirv,
            .path = path.string(),
            .resources = resources,
        };
    }
};

Optional<std::filesystem::path>
compiled_opengl_shader_path(const AssetPath& asset_path);

Optional<std::filesystem::path>
compiled_vulkan_shader_path(const AssetPath& asset_path);

Optional<std::filesystem::path>
shader_reflection_path(const AssetPath& asset_path);

Result<std::vector<std::byte>, ReaderError>
read_shader_binary(const std::filesystem::path& path);

using ShaderReflectionResult =
    Result<std::vector<ShaderResourceBinding>, std::string>;

ShaderReflectionResult parse_shader_reflection_bindings(std::string_view json);

Result<std::vector<ShaderResourceBinding>, AssetLoadError>
load_shader_reflection_bindings(const AssetPath& asset_path);

class ShaderLoader : public AssetLoader<Shader> {
  public:
    AssetLoadResult<Shader>
    load(Reader& reader, const LoadContext& context) override;
};

class ShaderAssetSource : public AssetSource {
  private:
    std::filesystem::path m_root;

  public:
    ShaderAssetSource();
    explicit ShaderAssetSource(std::filesystem::path root);

    std::string name() const override;
    bool exists(const std::filesystem::path& path) const override;
    Result<Reader, std::string>
    try_get_reader(const std::filesystem::path& path) const override;
};

class ShaderRef {
  private:
    std::variant<std::monostate, Handle<Shader>, AssetPath> m_source;

  public:
    ShaderRef() = default;
    ShaderRef(Handle<Shader> handle) : m_source(handle) {}
    ShaderRef(const AssetPath& path) : m_source(path) {}
    ShaderRef(const char* path) : m_source(AssetPath(path)) {}

    static ShaderRef default_shader() { return ShaderRef(); }

    bool is_default() const {
        return std::holds_alternative<std::monostate>(m_source);
    }

    Handle<Shader> resolve(AssetServer& asset_server) const {
        if (is_default()) {
            fatal("Cannot resolve default ShaderRef without pipeline context");
        }
        if (std::holds_alternative<Handle<Shader>>(m_source)) {
            return std::get<Handle<Shader>>(m_source);
        } else {
            return asset_server.load<Shader>(std::get<AssetPath>(m_source));
        }
    }
};

} // namespace fei
