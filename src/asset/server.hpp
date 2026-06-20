#pragma once
#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/loader.hpp"
#include "asset/path.hpp"
#include "asset/source.hpp"

#include <concepts>
#include <memory>
#include <string>
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
    AssetServer(AssetServer&&) noexcept = default;
    AssetServer& operator=(AssetServer&&) = default;

    template<typename T, std::derived_from<AssetLoader<T>> Loader>
    void add_loader() {
        if (m_app->has_resource<Assets<T>>()) {
            fatal("Asset loader for type {} already exists", type_name<T>());
        }
        (*m_app)
            .add_resource(
                Assets<T>(std::unique_ptr<AssetLoader<T>>(new Loader()))
            )
            .template add_event<AssetEvent<T>>()
            .add_systems(PostUpdate, Assets<T>::track_assets);
    }

    template<typename T>
    void add_without_loader() {
        if (m_app->has_resource<Assets<T>>()) {
            fatal("Asset type {} already exists", type_name<T>());
        }
        (*m_app)
            .add_resource(Assets<T>(nullptr))
            .template add_event<AssetEvent<T>>()
            .add_systems(PostUpdate, Assets<T>::track_assets);
    }

    template<typename T>
    Result<Handle<T>, AssetLoadError> try_load(const AssetPath& path) const {
        if (!m_app->has_resource<Assets<T>>()) {
            return failure(AssetLoadError(
                path,
                "No asset found for type: " + std::string(type_name<T>())
            ));
        }
        auto& assets = m_app->resource<Assets<T>>();
        auto source_name = path.source().value_or("default");
        if (!m_sources.contains(source_name)) {
            return failure(AssetLoadError(
                path,
                "No asset source found with name: " + source_name
            ));
        }
        auto& source = m_sources.at(source_name);
        if (!source->exists(path.path())) {
            return failure(AssetLoadError(
                path,
                "Asset not found at path: " + path.path().string() +
                    " in source: " + source_name
            ));
        }
        LoadContext context(*this, path);
        auto reader = source->get_reader(path.path());
        return assets.try_load(reader, context);
    }

    template<typename T>
    Handle<T> load(const AssetPath& path) const {
        auto result = try_load<T>(path);
        if (!result) {
            fatal(
                "Failed to load asset '{}': {}",
                result.error().path.as_string(),
                result.error().message
            );
        }
        return std::move(*result);
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

template<typename T>
Handle<T> LoadContext::load(const AssetPath& path) const {
    return m_asset_server.template load<T>(path);
}

template<typename T>
Result<Handle<T>, AssetLoadError>
LoadContext::try_load(const AssetPath& path) const {
    return m_asset_server.template try_load<T>(path);
}

} // namespace fei
