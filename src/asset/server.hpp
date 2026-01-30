#pragma once
#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/loader.hpp"
#include "asset/path.hpp"
#include "asset/source.hpp"

#include <concepts>
#include <memory>
#include <unordered_map>

namespace fei {

class AssetServer {
  private:
    App* m_app;
    std::unordered_map<std::string, std::unique_ptr<AssetSource>> m_sources;

  public:
    AssetServer(App* app) : m_app(app) {}

    // Delete copy constructor and copy assignment operator
    AssetServer(const AssetServer&) = delete;
    AssetServer& operator=(const AssetServer&) = delete;

    // Default move constructor and move assignment operator
    AssetServer(AssetServer&&) = default;
    AssetServer& operator=(AssetServer&&) = default;

    template<typename T, std::derived_from<AssetLoader<T>> Loader>
    void add_loader() {
        if (m_app->has_resource<Assets<T>>()) {
            fatal("Asset loader for type {} already exists", type_name<T>());
        }
        (*m_app)
            .add_resource(Assets<T>(std::unique_ptr<AssetLoader<T>>(new Loader()
            )))
            .template add_event<AssetEvent<T>>()
            .add_systems(PreUpdate, Assets<T>::track_assets);
    }

    template<typename T>
    void add_without_loader() {
        if (m_app->has_resource<Assets<T>>()) {
            fatal("Asset type {} already exists", type_name<T>());
        }
        (*m_app)
            .add_resource(Assets<T>(nullptr))
            .template add_event<AssetEvent<T>>()
            .add_systems(PreUpdate, Assets<T>::track_assets);
    }

    template<typename T>
    Handle<T> load(AssetPath path) {
        if (!m_app->has_resource<Assets<T>>()) {
            fatal("No asset found for type: {}", type_name<T>());
        }
        auto& assets = m_app->resource<Assets<T>>();
        auto source_name = path.source().value_or("default");
        if (!m_sources.contains(source_name)) {
            fatal("No asset source found with name: {}", source_name);
        }
        auto& source = m_sources.at(source_name);
        LoadContext context(path);
        auto reader = source->get_reader(path.path());
        return assets.load(reader, context);
    }

    template<std::derived_from<AssetSource> Source, typename... Args>
    void emplace_source(Args&&... args) {
        auto source = std::make_unique<Source>(std::forward<Args>(args)...);
        auto name = source->name();
        if (m_sources.contains(name)) {
            fatal("Asset source with name {} already exists", name);
        }
        m_sources.emplace(std::move(name), std::move(source));
    }
};

} // namespace fei
