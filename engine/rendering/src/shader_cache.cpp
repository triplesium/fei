#include "rendering/shader_cache.hpp"

#include "base/log.hpp"

#include <algorithm>
#include <chrono>
#include <future>
#include <system_error>

namespace ets {

namespace {

AssetPath canonical_shader_path(const AssetPath& path) {
    auto canonical = path.normalized();
    if (!canonical.source()) {
        canonical = canonical.with_source("shader");
    }
    if (*canonical.source() != "shader") {
        fatal(
            "ShaderCache: expected a shader:// path, got '{}'",
            path.as_string()
        );
    }
    return canonical;
}

} // namespace

ShaderCache::ShaderCache(
    const GraphicsDevice& device,
    ShaderVariantCompiler* variant_compiler
) : m_device(device), m_variant_compiler(variant_compiler) {}

ShaderCache::ShaderCache(
    const Assets<Shader>& shaders,
    const GraphicsDevice& device,
    ShaderVariantCompiler* variant_compiler
) : ShaderCache(device, variant_compiler) {
    for (const auto id : shaders.loaded_ids()) {
        set_shader(id, shaders.snapshot(id));
    }
    mark_shaders_initialized();
}

void ShaderCache::set_variant_compiler(ShaderVariantCompiler* compiler) {
    m_variant_compiler = compiler;
    if (m_variant_compiler != nullptr && m_source_snapshot) {
        m_variant_compiler->set_source_snapshot(m_source_snapshot);
    }
}

void ShaderCache::set_shader(AssetId id, std::shared_ptr<const Shader> shader) {
    if (!shader) {
        remove_shader(id);
        return;
    }
    m_shaders.insert_or_assign(id, std::move(shader));
    invalidate(ShaderSourceKey {id});
}

void ShaderCache::remove_shader(AssetId id) {
    m_shaders.erase(id);
    invalidate(ShaderSourceKey {id});
}

void ShaderCache::refresh_source_snapshot() {
    if (m_variant_compiler == nullptr) {
        return;
    }
    set_source_snapshot(
        ShaderSourceSnapshot::capture(
            m_variant_compiler->config().shader_sources
        )
    );
    m_next_source_snapshot_poll =
        std::chrono::steady_clock::now() + m_source_snapshot_poll_interval;
}

void ShaderCache::update_source_snapshot() {
    if (m_variant_compiler == nullptr) {
        return;
    }
    if (!m_source_snapshot) {
        refresh_source_snapshot();
        return;
    }

#ifdef __EMSCRIPTEN__
    // Browser shader sources are immutable MEMFS preload data. A page reload
    // installs a new snapshot without requiring a background polling thread.
    return;
#else
    using namespace std::chrono_literals;
    if (m_pending_source_snapshot.valid()) {
        if (m_pending_source_snapshot.wait_for(0ms) !=
            std::future_status::ready) {
            return;
        }

        auto pending = m_pending_source_snapshot.get();
        if (pending.generation == m_source_snapshot_generation &&
            pending.snapshot) {
            install_source_snapshot(std::move(pending.snapshot));
        }
    }

    const auto now = std::chrono::steady_clock::now();
    if (now < m_next_source_snapshot_poll) {
        return;
    }

    auto registry = m_variant_compiler->config().shader_sources;
    auto current = m_source_snapshot;
    const auto generation = m_source_snapshot_generation;
    m_pending_source_snapshot = std::async(
        std::launch::async,
        [registry = std::move(registry),
         current = std::move(current),
         generation]() mutable {
            auto snapshot = std::make_shared<const ShaderSourceSnapshot>(
                ShaderSourceSnapshot::capture(registry)
            );
            if (*current == *snapshot) {
                snapshot.reset();
            }
            return PendingSourceSnapshot {
                .generation = generation,
                .snapshot = std::move(snapshot),
            };
        }
    );
    m_next_source_snapshot_poll = now + m_source_snapshot_poll_interval;
#endif
}

void ShaderCache::set_source_snapshot_poll_interval(
    std::chrono::milliseconds interval
) {
    m_source_snapshot_poll_interval =
        std::max(interval, std::chrono::milliseconds::zero());
    m_next_source_snapshot_poll = std::chrono::steady_clock::now();
}

void ShaderCache::set_source_snapshot(ShaderSourceSnapshot snapshot) {
    if (m_source_snapshot && *m_source_snapshot == snapshot) {
        return;
    }
    install_source_snapshot(
        std::make_shared<const ShaderSourceSnapshot>(std::move(snapshot))
    );
}

void ShaderCache::install_source_snapshot(
    std::shared_ptr<const ShaderSourceSnapshot> snapshot
) {
    m_source_snapshot = std::move(snapshot);
    ++m_source_snapshot_generation;
    m_cache.clear();
    if (m_variant_compiler != nullptr) {
        m_variant_compiler->set_source_snapshot(m_source_snapshot);
    }
}

std::shared_ptr<ShaderModule> ShaderCache::get(const AssetId& id) {
    return get(id, {});
}

std::shared_ptr<ShaderModule>
ShaderCache::get(const AssetId& id, ShaderDefs defs) {
    auto shader = m_shaders.find(id);
    if (shader == m_shaders.end() || !shader->second) {
        fatal("ShaderCache: Shader asset '{}' not found", id);
    }
    auto stage = shader_stage_from_path(shader->second->path);
    if (!stage) {
        fatal(
            "ShaderCache: shader '{}' requires explicit stage and entry",
            shader->second->path.string()
        );
    }
    return get(
        id,
        *shader->second,
        stage.value(),
        default_shader_entry(stage.value()),
        std::move(defs)
    );
}

std::shared_ptr<ShaderModule> ShaderCache::get(
    const AssetId& id,
    ShaderStages stage,
    std::string entry,
    ShaderDefs defs
) {
    auto shader = m_shaders.find(id);
    if (shader == m_shaders.end() || !shader->second) {
        fatal("ShaderCache: Shader asset '{}' not found", id);
    }
    return get(id, *shader->second, stage, std::move(entry), std::move(defs));
}

std::shared_ptr<ShaderModule>
ShaderCache::get_or_compile(const AssetPath& path, ShaderDefs defs) {
    auto stage = shader_stage_from_path(path.path());
    if (!stage) {
        fatal(
            "ShaderCache: shader '{}' requires explicit stage and entry",
            path.as_string()
        );
    }
    return get_or_compile(
        path,
        stage.value(),
        default_shader_entry(stage.value()),
        std::move(defs)
    );
}

std::shared_ptr<ShaderModule> ShaderCache::get_or_compile(
    const AssetPath& path,
    ShaderStages stage,
    std::string entry,
    ShaderDefs defs
) {
    auto canonical = canonical_shader_path(path);
    if (!m_source_snapshot) {
        fatal(
            "ShaderCache: shader source snapshot is unavailable for '{}'",
            canonical.as_string()
        );
    }
    auto resolved = m_source_snapshot->resolve(canonical.path());
    if (!resolved) {
        fatal(
            "ShaderCache: shader source '{}' is absent from the Render World "
            "snapshot",
            canonical.as_string()
        );
    }
    auto source = m_source_snapshot->source(resolved->source_path);
    if (!source) {
        fatal(
            "ShaderCache: resolved shader source '{}' is absent from the "
            "Render World snapshot",
            resolved->source_path.string()
        );
    }
    return get(
        canonical,
        Shader {.path = canonical.path(), .source = *source},
        stage,
        std::move(entry),
        std::move(defs)
    );
}

std::shared_ptr<ShaderModule> ShaderCache::get(const ShaderRef& ref) {
    if (auto handle = ref.handle()) {
        return get(*handle);
    }
    if (auto path = ref.asset_path()) {
        return get_or_compile(*path);
    }
    fatal("ShaderCache: cannot resolve a default ShaderRef without context");
}

std::shared_ptr<ShaderModule>
ShaderCache::get(const ShaderRef& ref, ShaderDefs defs) {
    if (auto handle = ref.handle()) {
        return get(*handle, std::move(defs));
    }
    if (auto path = ref.asset_path()) {
        return get_or_compile(*path, std::move(defs));
    }
    fatal("ShaderCache: cannot resolve a default ShaderRef without context");
}

std::shared_ptr<ShaderModule> ShaderCache::get(
    const ShaderRef& ref,
    ShaderStages stage,
    std::string entry,
    ShaderDefs defs
) {
    if (auto handle = ref.handle()) {
        return get(*handle, stage, std::move(entry), std::move(defs));
    }
    if (auto path = ref.asset_path()) {
        return get_or_compile(*path, stage, std::move(entry), std::move(defs));
    }
    fatal("ShaderCache: cannot resolve a default ShaderRef without context");
}

std::string ShaderCache::default_shader_entry(ShaderStages stage) {
    switch (stage) {
        case ShaderStages::Vertex:
            return "vertex_main";
        case ShaderStages::Geometry:
            return "geometry_main";
        case ShaderStages::Fragment:
            return "fragment_main";
        case ShaderStages::Compute:
            return "compute_main";
        default:
            return "main";
    }
}

std::string
ShaderCache::normalized_shader_entry(ShaderStages stage, std::string entry) {
    if (entry.empty()) {
        return default_shader_entry(stage);
    }
    return entry;
}

std::vector<ShaderCache::ShaderFileDependency>
ShaderCache::make_file_dependencies(std::vector<std::filesystem::path> paths) {
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

bool ShaderCache::dependencies_are_current(
    const ShaderCacheEntry& entry
) const {
    if (m_source_snapshot) {
        return true;
    }
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

void ShaderCache::invalidate(const ShaderSourceKey& source) {
    std::erase_if(m_cache, [&](const auto& entry) {
        return entry.first.shader == source;
    });
}

std::shared_ptr<ShaderModule> ShaderCache::get(
    ShaderSourceKey source,
    const Shader& shader,
    ShaderStages stage,
    std::string entry,
    ShaderDefs defs
) {
    ShaderVariantKey key {
        .shader = std::move(source),
        .stage = stage,
        .entry = normalized_shader_entry(stage, std::move(entry)),
        .defs = normalized_shader_defs(std::move(defs)),
    };
    auto it = m_cache.find(key);
    if (it != m_cache.end() && dependencies_are_current(it->second)) {
        return it->second.module;
    }
    if (it != m_cache.end()) {
        m_cache.erase(it);
    }
    auto compiled = compile_description(shader, key.stage, key.entry, key.defs);
    auto shader_module = m_device.create_shader_module(compiled.description);
    m_cache.emplace(
        std::move(key),
        ShaderCacheEntry {
            .module = shader_module,
            .dependencies =
                m_source_snapshot ?
                    std::vector<ShaderFileDependency> {} :
                    make_file_dependencies(std::move(compiled.dependencies)),
        }
    );
    return shader_module;
}

ShaderCache::ShaderDescriptionWithDependencies ShaderCache::compile_description(
    const Shader& shader,
    ShaderStages stage,
    const std::string& entry,
    const ShaderDefs& defs
) {
    if (m_variant_compiler == nullptr) {
        fatal(
            "ShaderCache: cannot compile shader '{}' without a "
            "ShaderVariantCompiler",
            shader.path.string()
        );
    }
    auto compiled = m_variant_compiler->compile_with_dependencies(
        shader.path,
        shader.source,
        stage,
        entry,
        defs
    );
    if (!compiled) {
        auto error = std::move(compiled).error();
        fatal(
            "ShaderCache: failed to compile shader '{}': {}\n{}",
            shader.path.string(),
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

void extract_shaders(
    Extract<Optional<EventReaderRO<AssetEvent<Shader>>>> events,
    Extract<Optional<ResRO<Assets<Shader>>>> shaders,
    ResRW<ShaderCache> shader_cache
) {
    shader_cache->update_source_snapshot();

    const auto& source_shaders = shaders.get();
    if (!source_shaders) {
        return;
    }
    auto& source_events = events.get();
    if (!shader_cache->shaders_initialized()) {
        for (const auto id : (*source_shaders)->loaded_ids()) {
            shader_cache->set_shader(id, (*source_shaders)->snapshot(id));
        }
        shader_cache->mark_shaders_initialized();
        if (source_events) {
            while (source_events->next()) {}
        }
        return;
    }
    if (!source_events) {
        return;
    }
    while (auto event = source_events->next()) {
        switch (event->type) {
            case AssetEventType::Added:
            case AssetEventType::Modified:
                shader_cache->set_shader(
                    event->id,
                    (*source_shaders)->snapshot(event->id)
                );
                break;
            case AssetEventType::Removed:
            case AssetEventType::Failed:
                shader_cache->remove_shader(event->id);
                break;
        }
    }
}

} // namespace ets
