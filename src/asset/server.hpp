#pragma once
#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/loader.hpp"

#include <concepts>
#include <filesystem>
#include <memory>

namespace fei {

class AssetServer {
  private:
    std::filesystem::path m_assets_dir;
    App* m_app;

  public:
    AssetServer(App* app) : m_app(app) {
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

    template<typename T, std::derived_from<AssetLoader<T>> Loader>
    void add_loader() {
        if (m_app->has_resource<Assets<T>>()) {
            fatal("Asset loader for type {} already exists", type_name<T>());
        }
        (*m_app)
            .add_resource(Assets<T>(std::unique_ptr<AssetLoader<T>>(new Loader()
            )))
            .template add_event<AssetEvent<T>>()
            .add_system(PreUpdate, Assets<T>::track_assets);
    }

    template<typename T>
    void add_without_loader() {
        if (m_app->has_resource<Assets<T>>()) {
            fatal("Asset type {} already exists", type_name<T>());
        }
        (*m_app)
            .add_resource(Assets<T>(nullptr))
            .template add_event<AssetEvent<T>>()
            .add_system(PreUpdate, Assets<T>::track_assets);
    }

    template<typename T>
    Handle<T> load(const std::filesystem::path& path) {
        if (!m_app->has_resource<Assets<T>>()) {
            fatal("No asset found for type: {}", type_name<T>());
        }
        auto& assets = m_app->resource<Assets<T>>();
        auto full_path = m_assets_dir / path;
        return assets.load(full_path);
    }
};

} // namespace fei
