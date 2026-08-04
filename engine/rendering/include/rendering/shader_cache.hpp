#pragma once
#include "asset/assets.hpp"
#include "asset/event.hpp"
#include "asset/handle.hpp"
#include "asset/id.hpp"
#include "base/hash.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/shader_defs.hpp"
#include "graphics/shader_module.hpp"
#include "rendering/extract.hpp"
#include "rendering/shader.hpp"
#include "rendering/shader_compiler.hpp"

#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace fei {

using ShaderSourceKey = std::variant<AssetId, AssetPath>;

struct ShaderVariantKey {
    ShaderSourceKey shader;
    ShaderStages stage {ShaderStages::None};
    std::string entry;
    ShaderDefs defs;

    bool operator==(const ShaderVariantKey&) const = default;
};

} // namespace fei

namespace std {
template<>
struct hash<fei::ShaderVariantKey> { // NOLINT(readability-identifier-naming)
    std::size_t operator()(const fei::ShaderVariantKey& key) const {
        std::size_t shader_hash = key.shader.index();
        std::visit(
            [&](const auto& shader) {
                fei::hash_combine(shader_hash, shader);
            },
            key.shader
        );
        return fei::hash_combine_all(
            shader_hash,
            key.stage,
            key.entry,
            key.defs
        );
    }
};
} // namespace std

namespace fei {

class ShaderCache {
  private:
    struct PendingSourceSnapshot {
        std::size_t generation {0};
        std::shared_ptr<const ShaderSourceSnapshot> snapshot;
    };

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
    std::unordered_map<AssetId, std::shared_ptr<const Shader>> m_shaders;
    const GraphicsDevice& m_device;
    ShaderVariantCompiler* m_variant_compiler {nullptr};
    std::shared_ptr<const ShaderSourceSnapshot> m_source_snapshot;
    std::future<PendingSourceSnapshot> m_pending_source_snapshot;
    std::chrono::steady_clock::time_point m_next_source_snapshot_poll;
    std::chrono::milliseconds m_source_snapshot_poll_interval {
        std::chrono::seconds(1)
    };
    std::size_t m_source_snapshot_generation {0};
    bool m_shaders_initialized {false};

  public:
    explicit ShaderCache(
        const GraphicsDevice& device,
        ShaderVariantCompiler* variant_compiler = nullptr
    );

    ShaderCache(
        const Assets<Shader>& shaders,
        const GraphicsDevice& device,
        ShaderVariantCompiler* variant_compiler = nullptr
    );

    void set_variant_compiler(ShaderVariantCompiler* compiler);

    void set_shader(AssetId id, std::shared_ptr<const Shader> shader);
    void remove_shader(AssetId id);

    [[nodiscard]] bool shaders_initialized() const {
        return m_shaders_initialized;
    }
    void mark_shaders_initialized() { m_shaders_initialized = true; }

    void refresh_source_snapshot();
    void update_source_snapshot();
    void set_source_snapshot_poll_interval(std::chrono::milliseconds interval);
    void set_source_snapshot(ShaderSourceSnapshot snapshot);

    std::shared_ptr<ShaderModule> get(const AssetId& id);
    std::shared_ptr<ShaderModule> get(const AssetId& id, ShaderDefs defs);
    std::shared_ptr<ShaderModule>
    get(const AssetId& id,
        ShaderStages stage,
        std::string entry,
        ShaderDefs defs = {});

    std::shared_ptr<ShaderModule>
    get_or_compile(const AssetPath& path, ShaderDefs defs = {});
    std::shared_ptr<ShaderModule> get_or_compile(
        const AssetPath& path,
        ShaderStages stage,
        std::string entry,
        ShaderDefs defs = {}
    );

    std::shared_ptr<ShaderModule> get(Handle<Shader> handle) {
        return get(handle.id());
    }
    std::shared_ptr<ShaderModule> get(Handle<Shader> handle, ShaderDefs defs) {
        return get(handle.id(), std::move(defs));
    }
    std::shared_ptr<ShaderModule>
    get(Handle<Shader> handle,
        ShaderStages stage,
        std::string entry,
        ShaderDefs defs = {}) {
        return get(handle.id(), stage, std::move(entry), std::move(defs));
    }

    std::shared_ptr<ShaderModule> get(const ShaderRef& ref);
    std::shared_ptr<ShaderModule> get(const ShaderRef& ref, ShaderDefs defs);
    std::shared_ptr<ShaderModule>
    get(const ShaderRef& ref,
        ShaderStages stage,
        std::string entry,
        ShaderDefs defs = {});

  private:
    static std::string default_shader_entry(ShaderStages stage);
    static std::string
    normalized_shader_entry(ShaderStages stage, std::string entry);
    static std::vector<ShaderFileDependency>
    make_file_dependencies(std::vector<std::filesystem::path> paths);

    bool dependencies_are_current(const ShaderCacheEntry& entry) const;
    void invalidate(const ShaderSourceKey& source);
    void install_source_snapshot(
        std::shared_ptr<const ShaderSourceSnapshot> snapshot
    );

    std::shared_ptr<ShaderModule>
    get(ShaderSourceKey source,
        const Shader& shader,
        ShaderStages stage,
        std::string entry,
        ShaderDefs defs);

    ShaderDescriptionWithDependencies compile_description(
        const Shader& shader,
        ShaderStages stage,
        const std::string& entry,
        const ShaderDefs& defs
    );
};

void extract_shaders(
    Extract<Optional<EventReaderRO<AssetEvent<Shader>>>> events,
    Extract<Optional<ResRO<Assets<Shader>>>> shaders,
    ResRW<ShaderCache> shader_cache
);

} // namespace fei
