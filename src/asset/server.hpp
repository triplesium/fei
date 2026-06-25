#pragma once
#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/loader.hpp"
#include "asset/path.hpp"
#include "asset/request.hpp"
#include "asset/source.hpp"
#include "asset/systems.hpp"
#include "ecs/system_config.hpp"
#include "task/plugin.hpp"

#include <concepts>
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fei {

struct AssetLoadFailure {
    AssetKey asset;
    AssetLoadError error;
};

class AssetServer {
  private:
    struct AssetTypeAccess {
        std::function<Optional<AssetLoadState>(AssetId)> load_state;
        std::function<Optional<AssetLoadError>(AssetId)> load_error;
        std::function<std::vector<AssetKey>(AssetId)> dependencies;
    };

    App* m_app;
    std::unordered_map<std::string, std::unique_ptr<AssetSource>> m_sources;
    std::unordered_map<TypeId, AssetTypeAccess> m_asset_types;

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
            .add_systems(
                PostUpdate,
                Assets<T>::apply_async_loads |
                    in_set<AssetSystems::ApplyAsyncLoads>(),
                Assets<T>::collect_unused |
                    in_set<AssetSystems::CollectUnused>(),
                Assets<T>::track_assets | in_set<AssetSystems::TrackAssets>()
            );
        register_asset_type_access<T>();
    }

    template<typename T>
    void add_without_loader() {
        if (m_app->has_resource<Assets<T>>()) {
            fatal("Asset type {} already exists", type_name<T>());
        }
        (*m_app)
            .add_resource(Assets<T>(nullptr))
            .template add_event<AssetEvent<T>>()
            .add_systems(
                PostUpdate,
                Assets<T>::apply_async_loads |
                    in_set<AssetSystems::ApplyAsyncLoads>(),
                Assets<T>::collect_unused |
                    in_set<AssetSystems::CollectUnused>(),
                Assets<T>::track_assets | in_set<AssetSystems::TrackAssets>()
            );
        register_asset_type_access<T>();
    }

    template<typename T>
    Result<Handle<T>, AssetLoadError> try_load(const AssetPath& path) {
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
        SyncLoadContext context(*this, path);
        auto reader = source->try_get_reader(path.path());
        if (!reader) {
            return failure(AssetLoadError(
                path,
                "Failed to read asset from source '" + source_name +
                    "': " + reader.error()
            ));
        }
        return assets.try_load(*reader, context);
    }

    template<typename T>
    Result<Handle<T>, AssetLoadError> try_load_async(const AssetPath& path) {
        if (!m_app->has_resource<Assets<T>>()) {
            return failure(AssetLoadError(
                path,
                "No asset found for type: " + std::string(type_name<T>())
            ));
        }

        auto& assets = m_app->resource<Assets<T>>();
        if (auto cached = assets.cached_handle(path)) {
            return std::move(*cached);
        }

        auto* loader = assets.loader();
        if (!loader) {
            return failure(AssetLoadError(
                path,
                "AssetLoader not set for " + path.as_string()
            ));
        }

        if (!m_app->has_resource<Tasks>()) {
            return failure(AssetLoadError(
                path,
                "Tasks resource not found for async asset loading"
            ));
        }

        auto source_name = path.source().value_or("default");
        if (!m_sources.contains(source_name)) {
            return failure(AssetLoadError(
                path,
                "No asset source found with name: " + source_name
            ));
        }
        auto* source = m_sources.at(source_name).get();
        if (!source->exists(path.path())) {
            return failure(AssetLoadError(
                path,
                "Asset not found at path: " + path.path().string() +
                    " in source: " + source_name
            ));
        }

        auto handle = assets.reserve_loading(path);
        auto id = handle.id();
        auto assets_state = assets.state();
        auto load_requests = m_app->has_resource<AssetLoadRequests>() ?
                                 m_app->resource<AssetLoadRequests>().sender() :
                                 std::shared_ptr<AssetLoadRequestSender> {};
        struct LoadTaskResult {
            AssetLoadResult<T> result;
            std::vector<AssetKey> dependencies;
        };
        m_app->resource<Tasks>().general().submit(
            [source, loader, source_name, path, load_requests]() mutable
                -> LoadTaskResult {
                auto reader = source->try_get_reader(path.path());
                if (!reader) {
                    return {
                        .result = failure(AssetLoadError(
                            path,
                            "Failed to read asset from source '" + source_name +
                                "': " + reader.error()
                        )),
                        .dependencies = {},
                    };
                }

                if (load_requests) {
                    AsyncLoadContext context(std::move(load_requests), path);
                    auto result = loader->load(*reader, context);
                    auto dependencies = context.dependencies();
                    return {
                        .result = std::move(result),
                        .dependencies = std::move(dependencies),
                    };
                }

                LoadContext context(path);
                auto result = loader->load(*reader, context);
                auto dependencies = context.dependencies();
                return {
                    .result = std::move(result),
                    .dependencies = std::move(dependencies),
                };
            },
            [assets_state,
             id,
             path](TaskResult<LoadTaskResult> result) mutable {
                if (!assets_state || !assets_state->assets) {
                    return;
                }

                auto& assets = *assets_state->assets;
                try {
                    auto load_result = std::move(result).value();
                    assets.enqueue_async_load_result(
                        id,
                        std::move(load_result.result),
                        std::move(load_result.dependencies)
                    );
                } catch (const std::exception& error) {
                    assets.enqueue_async_load_result(
                        id,
                        failure(AssetLoadError(path, error.what()))
                    );
                } catch (...) {
                    assets.enqueue_async_load_result(
                        id,
                        failure(AssetLoadError(
                            path,
                            "Unknown async asset load error"
                        ))
                    );
                }
            }
        );

        return std::move(handle);
    }

    template<typename T>
    Handle<T> load(const AssetPath& path) {
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

    template<typename T>
    Handle<T> load_async(const AssetPath& path) {
        auto result = try_load_async<T>(path);
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

    Optional<AssetLoadState> load_state(AssetKey key) const {
        auto it = m_asset_types.find(key.type);
        if (it == m_asset_types.end()) {
            return nullopt;
        }
        return it->second.load_state(key.id);
    }

    template<typename T>
    Optional<AssetLoadState> load_state(const Handle<T>& handle) const {
        return load_state(asset_key(handle));
    }

    Optional<AssetLoadError> load_error(AssetKey key) const {
        auto it = m_asset_types.find(key.type);
        if (it == m_asset_types.end()) {
            return nullopt;
        }
        return it->second.load_error(key.id);
    }

    template<typename T>
    Optional<AssetLoadError> load_error(const Handle<T>& handle) const {
        return load_error(asset_key(handle));
    }

    bool is_loaded(AssetKey key) const {
        auto state = load_state(key);
        return state && *state == AssetLoadState::Loaded;
    }

    template<typename T>
    bool is_loaded(const Handle<T>& handle) const {
        return is_loaded(asset_key(handle));
    }

    std::vector<AssetKey> dependencies(AssetKey key) const {
        auto it = m_asset_types.find(key.type);
        if (it == m_asset_types.end()) {
            return {};
        }
        return it->second.dependencies(key.id);
    }

    template<typename T>
    std::vector<AssetKey> dependencies(const Handle<T>& handle) const {
        return dependencies(asset_key(handle));
    }

    AssetLoadState dependency_load_state(AssetKey key) const {
        auto state = load_state(key);
        if (!state) {
            return AssetLoadState::Loading;
        }
        if (*state != AssetLoadState::Loaded) {
            return *state;
        }
        return aggregate_load_state(dependencies(key));
    }

    template<typename T>
    AssetLoadState dependency_load_state(const Handle<T>& handle) const {
        return dependency_load_state(asset_key(handle));
    }

    Optional<AssetLoadFailure> first_failed_dependency(AssetKey key) const {
        std::unordered_set<AssetKey> visited;
        visited.insert(key);
        return first_failed_dependency(key, visited);
    }

    template<typename T>
    Optional<AssetLoadFailure>
    first_failed_dependency(const Handle<T>& handle) const {
        return first_failed_dependency(asset_key(handle));
    }

    AssetLoadState recursive_dependency_load_state(AssetKey key) const {
        auto state = load_state(key);
        if (!state) {
            return AssetLoadState::Loading;
        }
        if (*state != AssetLoadState::Loaded) {
            return *state;
        }

        std::unordered_set<AssetKey> visited;
        visited.insert(key);
        return recursive_dependency_load_state(key, visited);
    }

    template<typename T>
    AssetLoadState
    recursive_dependency_load_state(const Handle<T>& handle) const {
        return recursive_dependency_load_state(asset_key(handle));
    }

    bool is_loaded_with_dependencies(AssetKey key) const {
        return is_loaded(key) &&
               recursive_dependency_load_state(key) == AssetLoadState::Loaded;
    }

    template<typename T>
    bool is_loaded_with_dependencies(const Handle<T>& handle) const {
        return is_loaded_with_dependencies(asset_key(handle));
    }

    template<typename T>
    static AssetKey asset_key(const Handle<T>& handle) {
        return AssetKey {
            .type = type_id<T>(),
            .id = handle.id(),
        };
    }

  private:
    template<typename T>
    void register_asset_type_access() {
        auto* app = m_app;
        m_asset_types[type_id<T>()] = AssetTypeAccess {
            .load_state = [app](AssetId id) -> Optional<AssetLoadState> {
                if (!app || !app->template has_resource<Assets<T>>()) {
                    return nullopt;
                }
                return app->template resource<Assets<T>>().load_state(id);
            },
            .load_error = [app](AssetId id) -> Optional<AssetLoadError> {
                if (!app || !app->template has_resource<Assets<T>>()) {
                    return nullopt;
                }
                auto error = app->template resource<Assets<T>>().load_error(id);
                if (!error) {
                    return nullopt;
                }
                return *error;
            },
            .dependencies = [app](AssetId id) -> std::vector<AssetKey> {
                if (!app || !app->template has_resource<Assets<T>>()) {
                    return {};
                }
                auto dependencies =
                    app->template resource<Assets<T>>().dependencies(id);
                if (!dependencies) {
                    return {};
                }
                return std::vector<AssetKey>(
                    dependencies->begin(),
                    dependencies->end()
                );
            },
        };
    }

    AssetLoadState
    aggregate_load_state(const std::vector<AssetKey>& keys) const {
        auto state = AssetLoadState::Loaded;
        for (auto key : keys) {
            auto dependency_state = load_state(key);
            if (!dependency_state) {
                return AssetLoadState::Loading;
            }
            if (*dependency_state == AssetLoadState::Failed) {
                return AssetLoadState::Failed;
            }
            if (*dependency_state == AssetLoadState::Loading) {
                state = AssetLoadState::Loading;
            }
        }
        return state;
    }

    Optional<AssetLoadFailure> first_failed_dependency(
        AssetKey key,
        std::unordered_set<AssetKey>& visited
    ) const {
        for (auto dependency : dependencies(key)) {
            if (!visited.insert(dependency).second) {
                continue;
            }

            auto dependency_state = load_state(dependency);
            if (!dependency_state) {
                continue;
            }
            if (*dependency_state == AssetLoadState::Failed) {
                auto error = load_error(dependency);
                if (!error) {
                    continue;
                }
                return AssetLoadFailure {
                    .asset = dependency,
                    .error = std::move(*error),
                };
            }
            if (*dependency_state != AssetLoadState::Loaded) {
                continue;
            }

            auto failed_dependency =
                first_failed_dependency(dependency, visited);
            if (failed_dependency) {
                return failed_dependency;
            }
        }
        return nullopt;
    }

    AssetLoadState recursive_dependency_load_state(
        AssetKey key,
        std::unordered_set<AssetKey>& visited
    ) const {
        auto state = AssetLoadState::Loaded;
        for (auto dependency : dependencies(key)) {
            if (!visited.insert(dependency).second) {
                continue;
            }

            auto dependency_state = load_state(dependency);
            if (!dependency_state) {
                return AssetLoadState::Loading;
            }
            if (*dependency_state == AssetLoadState::Failed) {
                return AssetLoadState::Failed;
            }
            if (*dependency_state == AssetLoadState::Loading) {
                state = AssetLoadState::Loading;
                continue;
            }

            auto child_state =
                recursive_dependency_load_state(dependency, visited);
            if (child_state == AssetLoadState::Failed) {
                return AssetLoadState::Failed;
            }
            if (child_state == AssetLoadState::Loading) {
                state = AssetLoadState::Loading;
            }
        }
        return state;
    }
};

template<typename T>
Handle<T> LoadContext::load(const AssetPath& path) const {
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

template<typename T>
Result<Handle<T>, AssetLoadError>
LoadContext::try_load(const AssetPath& path) const {
    if (auto* context = dynamic_cast<const SyncLoadContext*>(this)) {
        auto result = context->template try_load<T>(path);
        if (result) {
            add_dependency(AssetServer::asset_key(*result));
        }
        return result;
    }
    if (auto* context = dynamic_cast<const AsyncLoadContext*>(this)) {
        auto result = context->template try_load<T>(path);
        if (result) {
            add_dependency(AssetServer::asset_key(*result));
        }
        return result;
    }
    return failure(AssetLoadError(
        path,
        "LoadContext does not support asset dependency loading"
    ));
}

template<typename T>
Handle<T> SyncLoadContext::load(const AssetPath& path) const {
    return m_asset_server.template load<T>(path);
}

template<typename T>
Result<Handle<T>, AssetLoadError>
SyncLoadContext::try_load(const AssetPath& path) const {
    return m_asset_server.template try_load<T>(path);
}

template<typename T>
Handle<T> AsyncLoadContext::load(const AssetPath& path) const {
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

template<typename T>
Result<Handle<T>, AssetLoadError>
AsyncLoadContext::try_load(const AssetPath& path) const {
    if (!m_requests) {
        return failure(AssetLoadError(
            path,
            "Asset dependency request sender is not available"
        ));
    }
    return m_requests->template try_load<T>(path);
}

template<typename T>
Result<Handle<T>, AssetLoadError>
AssetLoadRequestSender::try_load(const AssetPath& path) {
    struct Pending {
        std::mutex mutex;
        std::condition_variable completed;
        bool ready {false};
        Result<Handle<T>, AssetLoadError> result;
    };

    auto pending = std::make_shared<Pending>();
    auto enqueued = enqueue(
        Request {
            .process =
                [pending, path](AssetServer& server) mutable {
                    auto result = server.template try_load_async<T>(path);
                    {
                        std::scoped_lock state_lock(pending->mutex);
                        pending->result = std::move(result);
                        pending->ready = true;
                    }
                    pending->completed.notify_one();
                },
            .cancel =
                [pending, path]() mutable {
                    {
                        std::scoped_lock state_lock(pending->mutex);
                        pending->result = failure(AssetLoadError(
                            path,
                            "Asset dependency request queue is closed"
                        ));
                        pending->ready = true;
                    }
                    pending->completed.notify_one();
                },
        }
    );
    if (!enqueued) {
        return failure(
            AssetLoadError(path, "Asset dependency request queue is closed")
        );
    }

    std::unique_lock lock(pending->mutex);
    pending->completed.wait(lock, [&]() {
        return pending->ready;
    });
    return std::move(pending->result);
}

template<typename T>
Result<Handle<T>, AssetLoadError>
AssetLoadRequests::try_load(const AssetPath& path) {
    if (!m_sender) {
        return failure(AssetLoadError(
            path,
            "Asset dependency request sender is not available"
        ));
    }
    return m_sender->template try_load<T>(path);
}

} // namespace fei
