#pragma once
#include "asset/event.hpp"
#include "asset/id.hpp"
#include "asset/io.hpp"
#include "asset/loader.hpp"
#include "asset/path.hpp"
#include "base/log.hpp"
#include "base/optional.hpp"
#include "ecs/event.hpp"
#include "ecs/system_params.hpp"
#include "refl/type.hpp"

#include <concepts>
#include <memory>
#include <unordered_map>
#include <vector>

namespace fei {

template<typename T>
class Handle;

template<typename T>
class Assets;

enum class AssetLoadState {
    Loading,
    Loaded,
    Failed,
};

template<typename T>
struct AssetsState {
    Assets<T>* assets = nullptr;
};

template<typename T>
class Assets {
  public:
    struct Entry {
        AssetId id;
        Optional<AssetPath> path;
        TypeId type_id;
        std::unique_ptr<T> asset;
        AssetLoadState state;
        Optional<AssetLoadError> error;
        int ref_count;
    };

    struct AsyncLoadResult {
        AssetId id;
        AssetLoadResult<T> result;
    };

  private:
    std::unordered_map<AssetId, Entry> m_assets;
    std::unordered_map<AssetPath, AssetId> m_cache;
    AssetId m_next_id = 0;
    std::unique_ptr<AssetLoader<T>> m_loader;
    TypeId m_type_id;
    std::vector<AssetEvent<T>> m_event_queue;
    std::vector<AsyncLoadResult> m_pending_async_results;
    std::shared_ptr<AssetsState<T>> m_state;

  public:
    Assets(std::unique_ptr<AssetLoader<T>> loader) :
        m_loader(std::move(loader)), m_type_id(type_id<T>()),
        m_state(std::make_shared<AssetsState<T>>()) {
        m_state->assets = this;
    }
    ~Assets() {
        if (m_state) {
            m_state->assets = nullptr;
        }
    }

    Assets(const Assets&) = delete;
    Assets& operator=(const Assets&) = delete;
    Assets(Assets&& other) noexcept :
        m_assets(std::move(other.m_assets)), m_cache(std::move(other.m_cache)),
        m_next_id(other.m_next_id), m_loader(std::move(other.m_loader)),
        m_type_id(other.m_type_id),
        m_event_queue(std::move(other.m_event_queue)),
        m_pending_async_results(std::move(other.m_pending_async_results)),
        m_state(std::move(other.m_state)) {
        if (m_state) {
            m_state->assets = this;
        }
    }
    Assets& operator=(Assets&& other) noexcept {
        if (this != &other) {
            m_assets = std::move(other.m_assets);
            m_cache = std::move(other.m_cache);
            m_next_id = other.m_next_id;
            m_loader = std::move(other.m_loader);
            m_type_id = other.m_type_id;
            m_event_queue = std::move(other.m_event_queue);
            m_pending_async_results = std::move(other.m_pending_async_results);
            m_state = std::move(other.m_state);
            if (m_state) {
                m_state->assets = this;
            }
        }
        return *this;
    }

    Result<Handle<T>, AssetLoadError>
    try_load(Reader& reader, const LoadContext& context) {
        if (!m_loader) {
            return failure(AssetLoadError(
                context.asset_path(),
                "AssetLoader not set for " + context.asset_path().as_string()
            ));
        }

        auto cached = m_cache.find(context.asset_path());
        if (cached != m_cache.end()) {
            AssetId cached_id = cached->second;
            auto entry = get_entry(cached_id);
            if (entry && entry->state != AssetLoadState::Failed) {
                return Handle<T>(cached_id, m_state);
            }
            m_cache.erase(cached);
        }

        auto asset = m_loader->load(reader, context);
        if (!asset) {
            return failure(std::move(asset.error()));
        }

        auto handle = add_loaded(std::move(*asset), context.asset_path());
        return std::move(handle);
    }

    Handle<T> load(Reader& reader, const LoadContext& context) {
        auto result = try_load(reader, context);
        if (!result) {
            fei::fatal(
                "Failed to load asset '{}': {}",
                result.error().path.as_string(),
                result.error().message
            );
        }
        return std::move(*result);
    }

    Handle<T> add(std::unique_ptr<T> asset) {
        return add_loaded(std::move(asset), nullopt);
    }

    Handle<T> reserve_loading(const AssetPath& path) {
        if (auto cached = cached_handle(path)) {
            return std::move(*cached);
        }

        AssetId id = m_next_id++;
        m_assets[id] = {
            .id = id,
            .path = path,
            .type_id = m_type_id,
            .asset = nullptr,
            .state = AssetLoadState::Loading,
            .error = nullopt,
            .ref_count = 0,
        };
        m_cache[path] = id;
        return Handle<T>(id, m_state);
    }

    bool finish_loading(AssetId id, std::unique_ptr<T> asset) {
        auto entry = get_entry(id);
        if (!entry || entry->state != AssetLoadState::Loading) {
            return false;
        }
        if (entry->ref_count <= 0) {
            unload(id);
            return false;
        }

        entry->asset = std::move(asset);
        entry->state = AssetLoadState::Loaded;
        entry->error = nullopt;
        if (entry->path) {
            m_cache[*entry->path] = id;
        }
        m_event_queue.push_back(
            AssetEvent<T> {
                .type = AssetEventType::Added,
                .id = id,
            }
        );
        return true;
    }

    bool fail_loading(AssetId id, AssetLoadError error) {
        auto entry = get_entry(id);
        if (!entry || entry->state != AssetLoadState::Loading) {
            return false;
        }
        if (entry->ref_count <= 0) {
            unload(id);
            return false;
        }

        erase_cache_entry(*entry);
        entry->asset.reset();
        entry->state = AssetLoadState::Failed;
        entry->error = std::move(error);
        m_event_queue.push_back(
            AssetEvent<T> {
                .type = AssetEventType::Failed,
                .id = id,
            }
        );
        return true;
    }

    void enqueue_async_load_result(AssetId id, AssetLoadResult<T> result) {
        m_pending_async_results.push_back(
            AsyncLoadResult {
                .id = id,
                .result = std::move(result),
            }
        );
    }

    Optional<Handle<T>> cached_handle(const AssetPath& path) {
        auto cached = m_cache.find(path);
        if (cached == m_cache.end()) {
            return nullopt;
        }

        AssetId cached_id = cached->second;
        auto entry = get_entry(cached_id);
        if (entry && entry->state != AssetLoadState::Failed) {
            return Handle<T>(cached_id, m_state);
        }

        m_cache.erase(cached);
        return nullopt;
    }

    template<typename... Args>
    Handle<T> emplace(Args&&... args) {
        return add(std::make_unique<T>(std::forward<Args>(args)...));
    }

    template<std::derived_from<T> U, typename... Args>
    Handle<T> emplace_derived(Args&&... args) {
        return add(std::make_unique<U>(std::forward<Args>(args)...));
    }

    void unload(AssetId id) {
        auto it = m_assets.find(id);
        if (it == m_assets.end()) {
            error("Asset not found: {}", id);
            return;
        }
        erase_cache_entry(it->second);
        m_assets.erase(it);
        m_event_queue.push_back(
            AssetEvent<T> {
                .type = AssetEventType::Removed,
                .id = id,
            }
        );
    }

    Optional<T&> get(Handle<T> handle);

    Optional<T&> get(AssetId id) {
        Optional<Entry&> entry = get_entry(id);
        if (!entry || entry->state != AssetLoadState::Loaded) {
            return {};
        }
        m_event_queue.push_back(
            AssetEvent<T> {
                .type = AssetEventType::Modified,
                .id = id,
            }
        );
        return *entry->asset;
    }

    Optional<const T&> get(Handle<T> handle) const {
        auto it = m_assets.find(handle.id());
        if (it != m_assets.end() &&
            it->second.state == AssetLoadState::Loaded) {
            return *it->second.asset;
        }
        return nullopt;
    }

    Optional<const T&> get(AssetId id) const {
        return get(Handle<T>(id, m_state));
    }

    Optional<AssetLoadState> load_state(AssetId id) const {
        auto entry = get_entry(id);
        if (!entry) {
            return nullopt;
        }
        return entry->state;
    }

    Optional<AssetLoadState> load_state(const Handle<T>& handle) const {
        return load_state(handle.id());
    }

    Optional<const AssetLoadError&> load_error(AssetId id) const {
        auto entry = get_entry(id);
        if (!entry || !entry->error) {
            return nullopt;
        }
        return *entry->error;
    }

    Optional<const AssetLoadError&> load_error(const Handle<T>& handle) const {
        return load_error(handle.id());
    }

    AssetLoader<T>* loader() const { return m_loader.get(); }

    std::shared_ptr<AssetsState<T>> state() const { return m_state; }

    void acquire(AssetId id) {
        auto entry = get_entry(id);
        if (entry) {
            entry->ref_count++;
        } else {
            error("Attempted to acquire non-existent asset: {}", id);
        }
    }

    void release(AssetId id) {
        auto entry = get_entry(id);
        if (entry) {
            entry->ref_count--;
            // TODO: Queue unloading, and unload after some time
            if (entry->ref_count == 0) {
                unload(id);
            }
        } else {
            error("Attempted to release non-existent asset: {}", id);
        }
    }

    static void apply_async_loads(Res<Assets<T>> assets) {
        std::vector<AsyncLoadResult> pending;
        pending.swap(assets->m_pending_async_results);

        for (auto& result : pending) {
            if (!result.result) {
                assets->fail_loading(
                    result.id,
                    std::move(result.result).error()
                );
                continue;
            }

            assets->finish_loading(result.id, std::move(result.result).value());
        }
    }

    static void
    track_assets(Res<Assets<T>> assets, EventWriter<AssetEvent<T>> events) {
        for (auto& event : assets->m_event_queue) {
            events.send(event);
        }
        assets->m_event_queue.clear();
    }

  private:
    Handle<T> add_loaded(std::unique_ptr<T> asset, Optional<AssetPath> path) {
        AssetId id = m_next_id++;
        m_assets[id] = {
            .id = id,
            .path = std::move(path),
            .type_id = m_type_id,
            .asset = std::move(asset),
            .state = AssetLoadState::Loaded,
            .error = nullopt,
            .ref_count = 0,
        };
        auto& entry = m_assets.at(id);
        if (entry.path) {
            m_cache[*entry.path] = id;
        }
        m_event_queue.push_back(
            AssetEvent<T> {
                .type = AssetEventType::Added,
                .id = id,
            }
        );
        return Handle<T>(id, m_state);
    }

    void erase_cache_entry(const Entry& entry) {
        if (!entry.path) {
            return;
        }
        auto cached = m_cache.find(*entry.path);
        if (cached != m_cache.end() && cached->second == entry.id) {
            m_cache.erase(cached);
        }
    }

    Optional<Entry&> get_entry(AssetId id) {
        auto it = m_assets.find(id);
        if (it == m_assets.end()) {
            return {};
        }
        return it->second;
    }

    Optional<const Entry&> get_entry(AssetId id) const {
        auto it = m_assets.find(id);
        if (it == m_assets.end()) {
            return {};
        }
        return it->second;
    }
};

} // namespace fei

#include "asset/handle.hpp"

namespace fei {

template<typename T>
Optional<T&> Assets<T>::get(Handle<T> handle) {
    return get(handle.id());
}

} // namespace fei
