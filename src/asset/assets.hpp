#pragma once
#include "asset/asset_loader.hpp"
#include "asset/handle.hpp"
#include "base/log.hpp"
#include "base/optional.hpp"
#include "refl/type.hpp"

#include <concepts>
#include <filesystem>
#include <unordered_map>

namespace fei {

template<typename T>
class Handle;

template<typename T>
class Assets {
  public:
    struct Entry {
        Optional<std::filesystem::path> path;
        TypeId type_id;
        T* asset;
        bool is_loaded;
        int ref_count;
    };

  private:
    std::unordered_map<HandleId, Entry> m_assets;
    std::unordered_map<std::filesystem::path, HandleId> m_cache;
    HandleId m_next_id = 0;
    std::unique_ptr<AssetLoader<T>> m_loader;
    TypeId m_type_id;

  public:
    Assets(std::unique_ptr<AssetLoader<T>> loader) :
        m_loader(std::move(loader)), m_type_id(type_id<T>()) {}
    ~Assets() {
        for (auto& [id, entry] : m_assets) {
            if (entry.is_loaded) {
                m_loader->unload(entry.asset);
            }
        }
    }

    Assets(const Assets&) = delete;
    Assets& operator=(const Assets&) = delete;
    Assets(Assets&&) = default;
    Assets& operator=(Assets&&) = default;

    Handle<T> load(const std::filesystem::path& path) {
        if (!m_loader) {
            fei::fatal("AssetLoader not set for {}", path.string());
        }
        if (m_cache.contains(path)) {
            HandleId cached_id = m_cache[path];
            return Handle<T>(cached_id, this);
        }
        T* asset = m_loader->load(path);
        if (!asset) {
            fei::fatal("Failed to load asset: {}", path.string());
        }
        HandleId id = m_next_id++;
        m_assets[id] = {
            .path = path,
            .type_id = m_type_id,
            .asset = asset,
            .is_loaded = true,
            .ref_count = 0,
        };
        return Handle<T>(id, this);
    }

    Handle<T> add(T asset) {
        HandleId id = m_next_id++;
        m_assets[id] = {
            .path = nullopt,
            .type_id = m_type_id,
            .asset = new T(std::move(asset)),
            .is_loaded = true,
            .ref_count = 0,
        };
        return Handle<T>(id, this);
    }

    template<typename... Args>
    Handle<T> emplace(Args&&... args) {
        HandleId id = m_next_id++;
        m_assets[id] = {
            .path = nullopt,
            .type_id = m_type_id,
            .asset = new T(std::forward<Args>(args)...),
            .is_loaded = true,
            .ref_count = 0,
        };
        return Handle<T>(id, this);
    }

    template<std::derived_from<T> U, typename... Args>
    Handle<T> emplace_derived(Args&&... args) {
        HandleId id = m_next_id++;
        m_assets[id] = {
            .path = nullopt,
            .type_id = m_type_id,
            .asset = new U(std::forward<Args>(args)...),
            .is_loaded = true,
            .ref_count = 0,
        };
        return Handle<T>(id, this);
    }

    void unload(HandleId id) {
        auto it = m_assets.find(id);
        if (it == m_assets.end()) {
            error("Asset not found: {}", id);
            return;
        }
        m_loader->unload(it->second.asset);
        m_assets.erase(it);
    }

    Optional<T&> get(Handle<T> handle);

    void acquire(HandleId id) {
        auto entry = get_entry(id);
        if (entry) {
            entry->ref_count++;
        } else {
            error("Attempted to acquire non-existent asset: {}", id);
        }
    }

    void release(HandleId id) {
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

  private:
    Optional<Entry&> get_entry(HandleId id) {
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
    auto entry = get_entry(handle.id());
    if (!entry || !entry->is_loaded) {
        return {};
    }
    return *static_cast<T*>(entry->asset);
}

} // namespace fei
