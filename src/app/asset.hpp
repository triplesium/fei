#pragma once

#include "app/plugin.hpp"
#include "base/log.hpp"
#include "base/optional.hpp"
#include "refl/type.hpp"

#include <concepts>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>

#ifndef FEI_ASSETS_PATH
#    define FEI_ASSETS_PATH ""
#endif

namespace fei {

using HandleId = std::uint32_t;

class AssetLoaderBase {
  public:
    virtual ~AssetLoaderBase() = default;
    virtual void* load(const std::filesystem::path& path) = 0;
    virtual void unload(void* asset) = 0;
};

template<typename T>
class AssetLoader : public AssetLoaderBase {
  public:
    virtual void* load(const std::filesystem::path& path) final {
        return load_asset(path);
    }
    virtual void unload(void* asset) final { delete static_cast<T*>(asset); }

    virtual T* load_asset(const std::filesystem::path& path) = 0;
};

struct AssetEntry {
    std::filesystem::path path;
    TypeId type_id;
    void* asset;
    bool is_loaded;
};

class Assets {
  private:
    std::unordered_map<HandleId, AssetEntry> m_assets;
    HandleId m_next_id = 0;
    std::unique_ptr<AssetLoaderBase> m_loader;
    TypeId m_type_id;

  public:
    Assets(TypeId type_id, std::unique_ptr<AssetLoaderBase> loader) :
        m_loader(std::move(loader)), m_type_id(type_id) {}

    ~Assets() {
        for (auto& [id, entry] : m_assets) {
            if (entry.is_loaded) {
                m_loader->unload(entry.asset);
            }
        }
    }

    // Delete copy constructor and copy assignment operator
    Assets(const Assets&) = delete;
    Assets& operator=(const Assets&) = delete;

    // Default move constructor and move assignment operator
    Assets(Assets&&) = default;
    Assets& operator=(Assets&&) = default;

    Optional<AssetEntry&> get_entry(HandleId id) {
        auto it = m_assets.find(id);
        if (it == m_assets.end()) {
            error("Asset not found: {}", id);
            return {};
        }
        return it->second;
    }

    // TODO: Async loading
    HandleId load(const std::filesystem::path& path) {
        if (!m_loader) {
            fatal("AssetLoader not set for {}", path.string());
        }
        void* asset = m_loader->load(path);
        if (!asset) {
            fatal("Failed to load asset: {}", path.string());
        }
        HandleId id = m_next_id++;
        m_assets[id] = {
            .path = path,
            .type_id = m_type_id,
            .asset = asset,
            .is_loaded = true,
        };
        return id;
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
};

template<typename T>
class Handle {
  private:
    HandleId m_id;
    Assets* m_assets;

  public:
    Handle(HandleId id, Assets* assets) : m_id(id), m_assets(assets) {}
    HandleId id() const { return m_id; }

    Optional<T&> get() const {
        auto entry = m_assets->get_entry(m_id);
        if (entry && entry->is_loaded) {
            return *static_cast<T*>(entry->asset);
        }
        return {};
    }
};

class AssetServer {
  private:
    std::filesystem::path m_assets_dir;
    std::unordered_map<TypeId, std::unique_ptr<Assets>> m_assets;

  public:
    AssetServer() {
        if (std::string(FEI_ASSETS_PATH).empty()) {
            set_assets_dir(std::filesystem::current_path());
        } else {
            set_assets_dir(FEI_ASSETS_PATH);
        }
    }

    // Delete copy constructor and copy assignment operator
    AssetServer(const AssetServer&) = delete;
    AssetServer& operator=(const AssetServer&) = delete;

    // Default move constructor and move assignment operator
    AssetServer(AssetServer&&) = default;
    AssetServer& operator=(AssetServer&&) = default;

    void set_assets_dir(const std::filesystem::path& path) {
        m_assets_dir = path;
    }

    const std::filesystem::path& assets_dir() const { return m_assets_dir; }

    template<typename T, std::derived_from<AssetLoaderBase> Loader>
    void add_loader() {
        auto it = m_assets.find(type_id<T>());
        if (it != m_assets.end()) {
            fatal("Asset loader for type {} already exists", type_name<T>());
        }
        m_assets.emplace(
            type_id<T>(),
            std::make_unique<Assets>(type_id<T>(), std::make_unique<Loader>())
        );
    }

    template<typename T>
    Handle<T> load(const std::filesystem::path& path) {
        auto it = m_assets.find(type_id<T>());
        if (it == m_assets.end()) {
            fatal("No asset found for type: {}", type_name<T>());
        }
        auto& assets = *it->second;
        auto full_path = m_assets_dir / path;
        return Handle<T>(assets.load(full_path), &assets);
    }
};

class AssetPlugin : public Plugin {
  public:
    virtual void setup(App& app) override { app.add_resource(AssetServer {}); }
};

} // namespace fei
