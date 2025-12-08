#pragma once
#include "asset/event.hpp"
#include "asset/handle.hpp"
#include "asset/id.hpp"
#include "asset/loader.hpp"
#include "base/log.hpp"
#include "base/optional.hpp"
#include "ecs/event.hpp"
#include "ecs/system_params.hpp"
#include "refl/type.hpp"

#include <concepts>
#include <expected>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

namespace fei {

template<typename T>
class Handle;

template<typename T>
class Assets {
  public:
    struct Entry {
        AssetId id;
        Optional<std::filesystem::path> path;
        TypeId type_id;
        std::unique_ptr<T> asset;
        bool is_loaded;
        int ref_count;
    };

  private:
    std::unordered_map<AssetId, Entry> m_assets;
    std::unordered_map<std::filesystem::path, AssetId> m_cache;
    AssetId m_next_id = 0;
    std::unique_ptr<AssetLoader<T>> m_loader;
    TypeId m_type_id;
    std::vector<AssetEvent<T>> m_event_queue;

  public:
    Assets(std::unique_ptr<AssetLoader<T>> loader) :
        m_loader(std::move(loader)), m_type_id(type_id<T>()) {}
    ~Assets() = default;

    Assets(const Assets&) = delete;
    Assets& operator=(const Assets&) = delete;
    Assets(Assets&&) = default;
    Assets& operator=(Assets&&) = default;

    Handle<T> load(const std::filesystem::path& path) {
        if (!m_loader) {
            fei::fatal("AssetLoader not set for {}", path.string());
        }
        if (m_cache.contains(path)) {
            AssetId cached_id = m_cache[path];
            return Handle<T>(cached_id, this);
        }
        std::expected<std::unique_ptr<T>, std::error_code> asset =
            m_loader->load(path);
        if (!asset) {
            fei::fatal("Failed to load asset: {}", path.string());
        }
        return add(std::move(*asset));
    }

    Handle<T> add(std::unique_ptr<T> asset) {
        AssetId id = m_next_id++;
        m_assets[id] = {
            .path = nullopt,
            .type_id = m_type_id,
            .asset = std::move(asset),
            .is_loaded = true,
            .ref_count = 0,
        };
        m_event_queue.push_back(AssetEvent<T> {
            .type = AssetEventType::Added,
            .id = id,
        });
        return Handle<T>(id, this);
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
        m_assets.erase(it);
        m_event_queue.push_back(AssetEvent<T> {
            .type = AssetEventType::Removed,
            .id = id,
        });
    }

    Optional<T&> get(Handle<T> handle);

    Optional<T&> get(AssetId id) { return get(Handle<T>(id, this)); }

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
            if (entry->ref_count == 0) {
                unload(id);
            }
        } else {
            error("Attempted to release non-existent asset: {}", id);
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
    Optional<Entry&> get_entry(AssetId id) {
        auto it = m_assets.find(id);
        if (it == m_assets.end()) {
            error("Asset not found: {}", id);
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
    Optional<Entry&> entry = get_entry(handle.id());
    if (!entry || !entry->is_loaded) {
        return {};
    }
    m_event_queue.push_back(AssetEvent<T> {
        .type = AssetEventType::Modified,
        .id = handle.id(),
    });
    return *entry->asset;
}

} // namespace fei
