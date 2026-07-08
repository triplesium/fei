#pragma once
#include "asset/handle.hpp"
#include "asset/io.hpp"
#include "asset/loader.hpp"
#include "asset/path.hpp"
#include "asset/server.hpp"
#include "asset/source.hpp"
#include "base/hash.hpp"
#include "base/log.hpp"
#include "base/optional.hpp"
#include "base/result.hpp"
#include "graphics/enums.hpp"

#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace fei {

struct Shader {
    std::filesystem::path path;
    std::string source;
};

struct ShaderSourceRoot {
    std::string prefix;
    std::filesystem::path root;
};

struct ResolvedShaderSource {
    std::string prefix;
    std::filesystem::path root;
    std::filesystem::path relative_path;
    std::filesystem::path source_path;
};

class ShaderSourceRegistry {
  private:
    std::vector<ShaderSourceRoot> m_roots;

  public:
    void add_root(std::string prefix, std::filesystem::path root);

    [[nodiscard]] bool empty() const { return m_roots.empty(); }

    [[nodiscard]] Optional<ResolvedShaderSource>
    resolve(const std::filesystem::path& path) const;

    [[nodiscard]] std::vector<std::filesystem::path> roots() const;
};

ShaderSourceRegistry generated_shader_source_registry();

Optional<ShaderStages>
shader_stage_from_path(const std::filesystem::path& path);

class ShaderLoader : public AssetLoader<Shader> {
  public:
    AssetLoadResult<Shader>
    load(Reader& reader, const LoadContext& context) override;
};

class ShaderAssetSource : public AssetSource {
  private:
    ShaderSourceRegistry m_registry;

  public:
    ShaderAssetSource();
    explicit ShaderAssetSource(std::filesystem::path root);
    explicit ShaderAssetSource(ShaderSourceRegistry registry);

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

    Optional<const AssetPath&> asset_path() const {
        if (!std::holds_alternative<AssetPath>(m_source)) {
            return nullopt;
        }
        return std::get<AssetPath>(m_source);
    }

    std::size_t hash() const {
        std::size_t seed = 0;
        hash_combine(seed, m_source.index());
        if (std::holds_alternative<Handle<Shader>>(m_source)) {
            hash_combine(seed, std::get<Handle<Shader>>(m_source).id());
        } else if (std::holds_alternative<AssetPath>(m_source)) {
            hash_combine(seed, std::get<AssetPath>(m_source));
        }
        return seed;
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
