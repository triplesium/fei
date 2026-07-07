#pragma once
#include "asset/assets.hpp"
#include "asset/handle.hpp"
#include "asset/id.hpp"
#include "asset/server.hpp"
#include "base/hash.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/shader_defs.hpp"
#include "graphics/shader_module.hpp"
#include "rendering/shader.hpp"
#include "rendering/shader_compiler.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fei {

struct ShaderVariantKey {
    AssetId shader {invalid_asset_id};
    std::string source;
    ShaderDefs defs;

    bool operator==(const ShaderVariantKey&) const = default;
};

} // namespace fei

namespace std {
template<>
struct hash<fei::ShaderVariantKey> { // NOLINT(readability-identifier-naming)
    std::size_t operator()(const fei::ShaderVariantKey& key) const {
        return fei::hash_combine_all(key.shader, key.source, key.defs);
    }
};
} // namespace std

namespace fei {

class ShaderCache {
  private:
    struct ShaderFileDependency {
        std::filesystem::path path;
        std::filesystem::file_time_type modified_time;
    };

    struct ShaderCacheEntry {
        std::shared_ptr<ShaderModule> module;
        std::vector<ShaderFileDependency> dependencies;
    };

    struct ShaderDescriptionWithDependencies {
        ShaderDescription description;
        std::vector<std::filesystem::path> dependencies;
    };

    std::unordered_map<ShaderVariantKey, ShaderCacheEntry> m_cache;
    AssetServer& m_asset_server;
    Assets<Shader>& m_shaders;
    const GraphicsDevice& m_device;
    ShaderVariantCompiler* m_variant_compiler {nullptr};

  public:
    ShaderCache(
        AssetServer& asset_server,
        Assets<Shader>& shaders,
        const GraphicsDevice& device,
        ShaderVariantCompiler* variant_compiler = nullptr
    ) :
        m_asset_server(asset_server), m_shaders(shaders), m_device(device),
        m_variant_compiler(variant_compiler) {}

    void set_variant_compiler(ShaderVariantCompiler* compiler) {
        m_variant_compiler = compiler;
    }

    std::shared_ptr<ShaderModule> get(const AssetId& id) { return get(id, {}); }

    std::shared_ptr<ShaderModule> get(const AssetId& id, ShaderDefs defs) {
        ShaderVariantKey key {
            .shader = id,
            .defs = normalized_shader_defs(std::move(defs)),
        };
        auto it = m_cache.find(key);
        if (it != m_cache.end() && dependencies_are_current(it->second)) {
            return it->second.module;
        }
        if (it != m_cache.end()) {
            m_cache.erase(it);
        }
        auto shader_asset = m_shaders.get(id);
        if (!shader_asset) {
            fatal("ShaderCache: Shader asset '{}' not found", id);
        }
        auto compiled = make_description(*shader_asset, key.defs);
        auto shader_module =
            m_device.create_shader_module(compiled.description);
        m_cache.emplace(
            std::move(key),
            ShaderCacheEntry {
                .module = shader_module,
                .dependencies =
                    make_file_dependencies(std::move(compiled.dependencies)),
            }
        );
        return shader_module;
    }

    std::shared_ptr<ShaderModule>
    get_or_compile(const AssetPath& path, ShaderDefs defs = {}) {
        ShaderVariantKey key {
            .shader = invalid_asset_id,
            .source = path.as_string(),
            .defs = normalized_shader_defs(std::move(defs)),
        };
        auto it = m_cache.find(key);
        if (it != m_cache.end() && dependencies_are_current(it->second)) {
            return it->second.module;
        }
        if (it != m_cache.end()) {
            m_cache.erase(it);
        }

        auto compiled =
            compile_description(path.path(), key.defs, path.as_string());
        auto shader_module =
            m_device.create_shader_module(compiled.description);
        m_cache.emplace(
            std::move(key),
            ShaderCacheEntry {
                .module = shader_module,
                .dependencies =
                    make_file_dependencies(std::move(compiled.dependencies)),
            }
        );
        return shader_module;
    }

    std::shared_ptr<ShaderModule> get(Handle<Shader> handle) {
        return get(handle.id());
    }

    std::shared_ptr<ShaderModule> get(Handle<Shader> handle, ShaderDefs defs) {
        return get(handle.id(), std::move(defs));
    }

    std::shared_ptr<ShaderModule> get(const ShaderRef& ref) {
        return get(ref.resolve(m_asset_server));
    }

    std::shared_ptr<ShaderModule> get(const ShaderRef& ref, ShaderDefs defs) {
        if (!defs.empty()) {
            if (auto path = ref.asset_path()) {
                return get_or_compile(path.value(), std::move(defs));
            }
        }
        return get(ref.resolve(m_asset_server), std::move(defs));
    }

  private:
    static std::vector<ShaderFileDependency>
    make_file_dependencies(std::vector<std::filesystem::path> paths) {
        std::vector<ShaderFileDependency> dependencies;
        dependencies.reserve(paths.size());
        for (auto& path : paths) {
            std::error_code error;
            auto modified_time = std::filesystem::last_write_time(path, error);
            if (error) {
                continue;
            }
            dependencies.push_back(
                ShaderFileDependency {
                    .path = std::move(path),
                    .modified_time = modified_time,
                }
            );
        }
        return dependencies;
    }

    static bool dependencies_are_current(const ShaderCacheEntry& entry) {
        for (const auto& dependency : entry.dependencies) {
            std::error_code error;
            auto modified_time =
                std::filesystem::last_write_time(dependency.path, error);
            if (error || modified_time != dependency.modified_time) {
                return false;
            }
        }
        return true;
    }

    ShaderDescriptionWithDependencies
    make_description(const Shader& shader, const ShaderDefs& defs) {
        if (m_variant_compiler != nullptr && !defs.empty()) {
            return compile_description(shader.path, defs, shader.path.string());
        }

        auto description = shader.description();
        description.defs = defs;
        return ShaderDescriptionWithDependencies {
            .description = std::move(description),
        };
    }

    ShaderDescriptionWithDependencies compile_description(
        const std::filesystem::path& logical_path,
        const ShaderDefs& defs,
        const std::string& display_path
    ) {
        if (m_variant_compiler == nullptr) {
            fatal(
                "ShaderCache: cannot compile shader variant '{}' without a "
                "ShaderVariantCompiler",
                display_path
            );
        }

        auto compiled =
            m_variant_compiler->compile_with_dependencies(logical_path, defs);
        if (!compiled) {
            auto error = std::move(compiled).error();
            fatal(
                "ShaderCache: failed to compile shader variant '{}': {}\n{}",
                display_path,
                error.message,
                error.diagnostics
            );
        }
        auto output = std::move(compiled).value();
        return ShaderDescriptionWithDependencies {
            .description = std::move(output.description),
            .dependencies = std::move(output.dependencies),
        };
    }
};

} // namespace fei
