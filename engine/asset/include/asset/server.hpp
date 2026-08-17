#pragma once
#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/database.hpp"
#include "asset/loader.hpp"
#include "asset/path.hpp"
#include "asset/reference.hpp"
#include "asset/request.hpp"
#include "asset/source.hpp"
#include "asset/systems.hpp"
#include "ecs/system_config.hpp"
#include "refl/val.hpp"
#include "task/plugin.hpp"

#include <algorithm>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fei {

struct AssetLoadFailure {
    AssetKey asset;
    AssetLoadError error;
};

struct AssetTypeError {
    TypeId type;
    std::string message;
};

FEI_REFLECT(Resource)
class AssetServer {
  private:
    struct AssetTypeAccess {
        std::function<UntypedHandle(AssetServer&, const AssetPath&)> load;
        std::function<UntypedHandle(AssetServer&, const AssetPath&)> load_async;
        std::function<Result<Val, AssetTypeError>(const UntypedHandle&)>
            handle_value;
        std::function<Optional<AssetId>(Ref)> handle_id;
        std::function<Optional<AssetLoadState>(AssetId)> load_state;
        std::function<Optional<AssetLoadError>(AssetId)> load_error;
        std::function<std::vector<AssetKey>(AssetId)> dependencies;
        std::function<bool(const AssetPath&, const AssetPath&)> remap_path;
        std::function<std::size_t(const AssetPath&)> remove_path;
    };

    App* m_app;
    std::string m_default_source;
    std::unordered_map<std::string, std::unique_ptr<AssetSource>> m_sources;
    std::unordered_map<TypeId, AssetTypeAccess> m_asset_types;
    std::unordered_map<TypeId, TypeId> m_asset_handle_types;

    [[nodiscard]] Optional<std::filesystem::path>
    imported_artifact_for(const AssetPath& path, std::string_view kind) const {
        if (kind.empty() || !path.source() || *path.source() != "project" ||
            !m_app->has_resource<AssetDatabase>()) {
            return nullopt;
        }
        return m_app->resource<AssetDatabase>().artifact_path(path, kind);
    }

  public:
    explicit AssetServer(App* app, std::string default_source = "project") :
        m_app(app), m_default_source(std::move(default_source)) {}

    // Delete copy constructor and copy assignment operator
    AssetServer(const AssetServer&) = delete;
    AssetServer& operator=(const AssetServer&) = delete;

    // Default move constructor and move assignment operator
    AssetServer(AssetServer&&) noexcept = default;
    AssetServer& operator=(AssetServer&&) = default;

    [[nodiscard]] const std::string& default_source() const {
        return m_default_source;
    }

    [[nodiscard]] bool has_source(std::string_view name) const {
        return m_sources.contains(std::string(name));
    }

    bool set_default_source(std::string source) {
        if (!has_source(source)) {
            return false;
        }
        m_default_source = std::move(source);
        return true;
    }

    [[nodiscard]] AssetPath canonicalize_path(const AssetPath& path) const {
        auto canonical = path.normalized();
        if (!canonical.source()) {
            canonical = canonical.with_source(m_default_source);
        }
        return canonical;
    }

    [[nodiscard]] AssetReference reference(const AssetPath& path) const {
        const auto canonical = canonicalize_path(path);
        Optional<AssetUuid> id;
        if (canonical.source() && *canonical.source() == "project" &&
            m_app->has_resource<AssetDatabase>()) {
            if (const auto* metadata =
                    m_app->resource<AssetDatabase>().metadata(canonical)) {
                id = metadata->id;
            }
        }
        return AssetReference {
            .id = id,
            .fallback_path = canonical,
        };
    }

    std::size_t
    remap_path(const AssetPath& source, const AssetPath& destination) {
        const auto canonical_source = canonicalize_path(source);
        const auto canonical_destination = canonicalize_path(destination);
        std::size_t remapped = 0;
        for (auto& [type, access] : m_asset_types) {
            (void)type;
            remapped +=
                access.remap_path(canonical_source, canonical_destination);
        }
        return remapped;
    }

    std::size_t remove_path(const AssetPath& path) {
        const auto canonical = canonicalize_path(path);
        std::size_t removed = 0;
        for (auto& [type, access] : m_asset_types) {
            (void)type;
            removed += access.remove_path(canonical);
        }
        return removed;
    }

    [[nodiscard]] Result<AssetPath, AssetLoadError>
    resolve(const AssetReference& reference) const {
        if (reference.id && m_app->has_resource<AssetDatabase>()) {
            if (auto current =
                    m_app->resource<AssetDatabase>().path(*reference.id)) {
                return canonicalize_path(*current);
            }
        }
        auto fallback = canonicalize_path(reference.fallback_path);
        if (fallback.is_unapproved()) {
            return failure(AssetLoadError(
                fallback,
                "Asset reference fallback escapes its source root"
            ));
        }
        return fallback;
    }

    [[nodiscard]] Result<AssetPath, AssetLoadError>
    resolve(AssetUuid id) const {
        const AssetPath unresolved("project://");
        if (!m_app->has_resource<AssetDatabase>()) {
            return failure(AssetLoadError(
                unresolved,
                "Asset database is not available while resolving UUID " +
                    id.as_string()
            ));
        }
        auto path = m_app->resource<AssetDatabase>().path(id);
        if (!path) {
            return failure(AssetLoadError(
                unresolved,
                "No project asset found for UUID " + id.as_string()
            ));
        }
        return canonicalize_path(*path);
    }

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

    [[nodiscard]] bool has_asset_type(TypeId type) const {
        return m_asset_types.contains(type);
    }

    Result<UntypedHandle, AssetTypeError>
    load(TypeId type, const AssetPath& path) {
        auto access = m_asset_types.find(type);
        if (access == m_asset_types.end()) {
            return failure(
                AssetTypeError {
                    .type = type,
                    .message = "No asset type registered for type id " +
                               std::to_string(type.id()),
                }
            );
        }
        return access->second.load(*this, path);
    }

    Result<UntypedHandle, AssetTypeError>
    load_async(TypeId type, const AssetPath& path) {
        auto access = m_asset_types.find(type);
        if (access == m_asset_types.end()) {
            return failure(
                AssetTypeError {
                    .type = type,
                    .message = "No asset type registered for type id " +
                               std::to_string(type.id()),
                }
            );
        }
        return access->second.load_async(*this, path);
    }

    Result<Val, AssetTypeError>
    handle_value(const UntypedHandle& handle) const {
        auto access = m_asset_types.find(handle.asset_type());
        if (access == m_asset_types.end()) {
            return failure(
                AssetTypeError {
                    .type = handle.asset_type(),
                    .message = "No asset type registered for type id " +
                               std::to_string(handle.asset_type().id()),
                }
            );
        }
        return access->second.handle_value(handle);
    }

    Result<AssetKey, AssetTypeError> asset_key(Ref handle) const {
        auto asset_type = m_asset_handle_types.find(handle.type_id());
        if (asset_type == m_asset_handle_types.end()) {
            return failure(
                AssetTypeError {
                    .type = handle.type_id(),
                    .message = "Type id " +
                               std::to_string(handle.type_id().id()) +
                               " is not a registered asset handle",
                }
            );
        }
        const auto access = m_asset_types.find(asset_type->second);
        if (access == m_asset_types.end()) {
            return failure(
                AssetTypeError {
                    .type = asset_type->second,
                    .message = "No asset type registered for type id " +
                               std::to_string(asset_type->second.id()),
                }
            );
        }
        auto id = access->second.handle_id(handle);
        if (!id) {
            return failure(
                AssetTypeError {
                    .type = handle.type_id(),
                    .message = "Value does not contain the expected asset "
                               "handle type",
                }
            );
        }
        return AssetKey {.type = asset_type->second, .id = *id};
    }

    template<typename T>
    Handle<T> load(const AssetPath& path) {
        if (!m_app->has_resource<Assets<T>>()) {
            fatal("No asset found for type: {}", type_name<T>());
        }
        const auto asset_path = canonicalize_path(path);
        auto& assets = m_app->resource<Assets<T>>();
        if (auto cached = assets.cached_handle(asset_path)) {
            return std::move(*cached);
        }
        auto* loader = assets.loader();
        if (!loader) {
            return assets.add_failed(AssetLoadError(
                asset_path,
                "AssetLoader not set for " + asset_path.as_string()
            ));
        }
        if (asset_path.is_unapproved()) {
            return assets.add_failed(AssetLoadError(
                asset_path,
                "Asset path escapes its source root: " + asset_path.as_string()
            ));
        }
        const auto& source_name = *asset_path.source();
        if (!m_sources.contains(source_name)) {
            return assets.add_failed(AssetLoadError(
                asset_path,
                "No asset source found with name: " + source_name
            ));
        }
        auto& source = m_sources.at(source_name);
        if (!source->exists(asset_path.path())) {
            return assets.add_failed(AssetLoadError(
                asset_path,
                "Asset not found at path: " + asset_path.path().string() +
                    " in source: " + source_name
            ));
        }
        SyncLoadContext context(*this, asset_path);
        const auto artifact =
            imported_artifact_for(asset_path, loader->artifact_kind());
        auto reader = artifact ? [&]() -> Result<Reader, std::string> {
            auto imported = Reader::from_file(*artifact);
            if (!imported) {
                return failure(std::move(imported.error().message));
            }
            return std::move(*imported);
        }() :
            source->try_get_reader(asset_path.path());
        if (!reader) {
            return assets.add_failed(AssetLoadError(
                asset_path,
                artifact ? "Failed to read imported artifact '" +
                               artifact->string() + "': " + reader.error() :
                           "Failed to read asset from source '" + source_name +
                               "': " + reader.error()
            ));
        }
        return assets.load(*reader, context);
    }

    template<typename T>
    Handle<T> load(const AssetReference& reference) {
        auto path = resolve(reference);
        if (!path) {
            if (!m_app->has_resource<Assets<T>>()) {
                fatal("No asset found for type: {}", type_name<T>());
            }
            return m_app->resource<Assets<T>>().add_failed(
                std::move(path.error())
            );
        }
        return load<T>(*path);
    }

    template<typename T>
    Handle<T> load(AssetUuid id) {
        auto path = resolve(id);
        if (!path) {
            if (!m_app->has_resource<Assets<T>>()) {
                fatal("No asset found for type: {}", type_name<T>());
            }
            return m_app->resource<Assets<T>>().add_failed(
                std::move(path.error())
            );
        }
        return load<T>(*path);
    }

    template<typename T>
    Result<Handle<T>, AssetLoadError> reload(const AssetPath& path) {
        if (!m_app->has_resource<Assets<T>>()) {
            fatal("No asset found for type: {}", type_name<T>());
        }
        const auto asset_path = canonicalize_path(path);
        auto& assets = m_app->resource<Assets<T>>();
        auto* loader = assets.loader();
        if (!loader) {
            return failure(AssetLoadError(
                asset_path,
                "AssetLoader not set for " + asset_path.as_string()
            ));
        }
        if (asset_path.is_unapproved()) {
            return failure(AssetLoadError(
                asset_path,
                "Asset path escapes its source root: " + asset_path.as_string()
            ));
        }
        const auto& source_name = *asset_path.source();
        if (!m_sources.contains(source_name)) {
            return failure(AssetLoadError(
                asset_path,
                "No asset source found with name: " + source_name
            ));
        }
        auto& source = m_sources.at(source_name);
        if (!source->exists(asset_path.path())) {
            return failure(AssetLoadError(
                asset_path,
                "Asset not found at path: " + asset_path.path().string() +
                    " in source: " + source_name
            ));
        }

        const auto artifact =
            imported_artifact_for(asset_path, loader->artifact_kind());
        auto reader = artifact ? [&]() -> Result<Reader, std::string> {
            auto imported = Reader::from_file(*artifact);
            if (!imported) {
                return failure(std::move(imported.error().message));
            }
            return std::move(*imported);
        }() :
            source->try_get_reader(asset_path.path());
        if (!reader) {
            return failure(AssetLoadError(
                asset_path,
                artifact ? "Failed to read imported artifact '" +
                               artifact->string() + "': " + reader.error() :
                           "Failed to read asset from source '" + source_name +
                               "': " + reader.error()
            ));
        }
        SyncLoadContext context(*this, asset_path);
        return assets.reload(*reader, context);
    }

    template<typename T>
    Result<bool, AssetLoadError> reload_if_loaded(const AssetPath& path) {
        if (!m_app->has_resource<Assets<T>>()) {
            fatal("No asset found for type: {}", type_name<T>());
        }
        const auto asset_path = canonicalize_path(path);
        auto& assets = m_app->resource<Assets<T>>();
        if (!assets.cached_handle(asset_path)) {
            return false;
        }
        auto reloaded = reload<T>(asset_path);
        if (!reloaded) {
            return failure(std::move(reloaded.error()));
        }
        return true;
    }

    template<typename T>
    Handle<T> load_async(const AssetPath& path) {
        if (!m_app->has_resource<Assets<T>>()) {
            fatal("No asset found for type: {}", type_name<T>());
        }

        const auto asset_path = canonicalize_path(path);
        auto& assets = m_app->resource<Assets<T>>();
        if (auto cached = assets.cached_handle(asset_path)) {
            return std::move(*cached);
        }

        auto* loader = assets.loader();
        if (!loader) {
            return assets.add_failed(AssetLoadError(
                asset_path,
                "AssetLoader not set for " + asset_path.as_string()
            ));
        }
        if (asset_path.is_unapproved()) {
            return assets.add_failed(AssetLoadError(
                asset_path,
                "Asset path escapes its source root: " + asset_path.as_string()
            ));
        }

        if (!m_app->has_resource<Tasks>()) {
            return assets.add_failed(AssetLoadError(
                asset_path,
                "Tasks resource not found for async asset loading"
            ));
        }

        const auto& source_name = *asset_path.source();
        if (!m_sources.contains(source_name)) {
            return assets.add_failed(AssetLoadError(
                asset_path,
                "No asset source found with name: " + source_name
            ));
        }
        auto* source = m_sources.at(source_name).get();
        if (!source->exists(asset_path.path())) {
            return assets.add_failed(AssetLoadError(
                asset_path,
                "Asset not found at path: " + asset_path.path().string() +
                    " in source: " + source_name
            ));
        }

        auto handle = assets.reserve_loading(asset_path);
        auto id = handle.id();
        auto assets_state = assets.state();
        auto load_requests = m_app->has_resource<AssetLoadRequests>() ?
                                 m_app->resource<AssetLoadRequests>().sender() :
                                 std::shared_ptr<AssetLoadRequestSender> {};
        const auto artifact =
            imported_artifact_for(asset_path, loader->artifact_kind());
        struct LoadTaskResult {
            AssetLoadResult<T> result;
            std::vector<AssetKey> dependencies;
            std::vector<AssetPath> loader_dependencies;
        };
        m_app->resource<Tasks>().general().submit(
            [source,
             loader,
             source_name,
             asset_path,
             artifact,
             load_requests]() mutable -> LoadTaskResult {
                auto reader = artifact ? [&]() -> Result<Reader, std::string> {
                    auto imported = Reader::from_file(*artifact);
                    if (!imported) {
                        return failure(std::move(imported.error().message));
                    }
                    return std::move(*imported);
                }() :
                    source->try_get_reader(asset_path.path());
                if (!reader) {
                    return {
                        .result = failure(AssetLoadError(
                            asset_path,
                            artifact ? "Failed to read imported artifact '" +
                                           artifact->string() +
                                           "': " + reader.error() :
                                       "Failed to read asset from source '" +
                                           source_name + "': " + reader.error()
                        )),
                        .dependencies = {},
                        .loader_dependencies = {},
                    };
                }

                if (load_requests) {
                    AsyncLoadContext context(
                        std::move(load_requests),
                        asset_path
                    );
                    auto result = loader->load(*reader, context);
                    auto dependencies = context.dependencies();
                    auto loader_dependencies = context.loader_dependencies();
                    return {
                        .result = std::move(result),
                        .dependencies = std::move(dependencies),
                        .loader_dependencies = std::move(loader_dependencies),
                    };
                }

                LoadContext context(asset_path);
                auto result = loader->load(*reader, context);
                auto dependencies = context.dependencies();
                auto loader_dependencies = context.loader_dependencies();
                return {
                    .result = std::move(result),
                    .dependencies = std::move(dependencies),
                    .loader_dependencies = std::move(loader_dependencies),
                };
            },
            [assets_state,
             id,
             asset_path](TaskResult<LoadTaskResult> result) mutable {
                if (!assets_state || !assets_state->assets) {
                    return;
                }

                auto& assets = *assets_state->assets;
                try {
                    auto load_result = std::move(result).value();
                    assets.enqueue_async_load_result(
                        id,
                        std::move(load_result.result),
                        std::move(load_result.dependencies),
                        std::move(load_result.loader_dependencies)
                    );
                } catch (const std::exception& error) {
                    assets.enqueue_async_load_result(
                        id,
                        failure(AssetLoadError(asset_path, error.what()))
                    );
                } catch (...) {
                    assets.enqueue_async_load_result(
                        id,
                        failure(AssetLoadError(
                            asset_path,
                            "Unknown async asset load error"
                        ))
                    );
                }
            }
        );

        return std::move(handle);
    }

    Result<std::vector<std::byte>, AssetLoadError>
    read_asset_bytes(const AssetPath& path) const {
        const auto asset_path = canonicalize_path(path);
        if (asset_path.is_unapproved()) {
            return failure(AssetLoadError(
                asset_path,
                "Asset path escapes its source root: " + asset_path.as_string()
            ));
        }
        const auto& source_name = *asset_path.source();
        const auto source = m_sources.find(source_name);
        if (source == m_sources.end()) {
            return failure(AssetLoadError(
                asset_path,
                "No asset source found with name: " + source_name
            ));
        }
        if (!source->second->exists(asset_path.path())) {
            return failure(AssetLoadError(
                asset_path,
                "Asset not found at path: " + asset_path.path().string() +
                    " in source: " + source_name
            ));
        }
        auto reader = source->second->try_get_reader(asset_path.path());
        if (!reader) {
            return failure(AssetLoadError(
                asset_path,
                "Failed to read asset from source '" + source_name +
                    "': " + reader.error()
            ));
        }
        std::vector<std::byte> bytes(reader->size());
        std::copy_n(reader->data(), reader->size(), bytes.data());
        return bytes;
    }

    template<typename T>
    Handle<T> add_asset(std::unique_ptr<T> asset) {
        if (!m_app->has_resource<Assets<T>>()) {
            fatal("No asset found for type: {}", type_name<T>());
        }
        return m_app->resource<Assets<T>>().add(std::move(asset));
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

    Result<std::vector<AssetEntry>, std::string>
    list(const AssetPath& directory, bool recursive = false) const {
        const auto asset_path = canonicalize_path(directory);
        if (asset_path.is_unapproved()) {
            return failure(
                "Asset path escapes its source root: " + asset_path.as_string()
            );
        }
        const auto& source_name = *asset_path.source();
        const auto source = m_sources.find(source_name);
        if (source == m_sources.end()) {
            return failure("No asset source found with name: " + source_name);
        }
        return source->second->list(asset_path.path(), recursive);
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

    Optional<AssetLoadState> load_state(const UntypedHandle& handle) const {
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

    Optional<AssetLoadError> load_error(const UntypedHandle& handle) const {
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

    bool is_loaded(const UntypedHandle& handle) const {
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

    std::vector<AssetKey> dependencies(const UntypedHandle& handle) const {
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

    AssetLoadState dependency_load_state(const UntypedHandle& handle) const {
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

    Optional<AssetLoadFailure>
    first_failed_dependency(const UntypedHandle& handle) const {
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

    AssetLoadState
    recursive_dependency_load_state(const UntypedHandle& handle) const {
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

    bool is_loaded_with_dependencies(const UntypedHandle& handle) const {
        return is_loaded_with_dependencies(asset_key(handle));
    }

    template<typename T>
    static AssetKey asset_key(const Handle<T>& handle) {
        return AssetKey {
            .type = type_id<T>(),
            .id = handle.id(),
        };
    }

    static AssetKey asset_key(const UntypedHandle& handle) {
        return AssetKey {
            .type = handle.asset_type(),
            .id = handle.id(),
        };
    }

  private:
    template<typename T>
    void register_asset_type_access() {
        auto* app = m_app;
        m_asset_handle_types[type_id<Handle<T>>()] = type_id<T>();
        m_asset_types[type_id<T>()] = AssetTypeAccess {
            .load =
                [](AssetServer& server, const AssetPath& path) {
                    return server.template load<T>(path).untyped();
                },
            .load_async =
                [](AssetServer& server, const AssetPath& path) {
                    return server.template load_async<T>(path).untyped();
                },
            .handle_value =
                [](const UntypedHandle& handle) -> Result<Val, AssetTypeError> {
                auto typed = handle.template try_typed<T>();
                if (!typed) {
                    return failure(
                        AssetTypeError {
                            .type = handle.asset_type(),
                            .message = "Untyped asset handle does not contain "
                                       "the expected asset type",
                        }
                    );
                }
                return make_val<Handle<T>>(std::move(*typed));
            },
            .handle_id = [](Ref handle) -> Optional<AssetId> {
                const auto* typed = handle.template try_get_const<Handle<T>>();
                if (typed == nullptr) {
                    return nullopt;
                }
                return typed->id();
            },
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
            .remap_path =
                [app](const AssetPath& source, const AssetPath& destination) {
                    if (!app || !app->template has_resource<Assets<T>>()) {
                        return false;
                    }
                    return app->template resource<Assets<T>>().remap_path(
                        source,
                        destination
                    );
                },
            .remove_path =
                [app](const AssetPath& path) {
                    if (!app || !app->template has_resource<Assets<T>>()) {
                        return std::size_t {0};
                    }
                    return app->template resource<Assets<T>>().remove_path(
                        path
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
    Handle<T> handle;
    if (auto* context = dynamic_cast<const SyncLoadContext*>(this)) {
        handle = context->template load<T>(path);
    } else if (auto* context = dynamic_cast<const AsyncLoadContext*>(this)) {
        handle = context->template load<T>(path);
    } else {
        throw std::runtime_error(
            "LoadContext does not support asset dependency loading"
        );
    }
    add_dependency(AssetServer::asset_key(handle));
    return handle;
}

inline void
LoadContext::add_loader_dependency(const AssetPath& dependency) const {
    if (std::find(
            m_loader_dependencies.begin(),
            m_loader_dependencies.end(),
            dependency
        ) == m_loader_dependencies.end()) {
        m_loader_dependencies.push_back(dependency);
    }
}

inline Result<std::vector<std::byte>, AssetLoadError>
LoadContext::read_asset_bytes(const AssetPath& path) const {
    Result<std::vector<std::byte>, AssetLoadError> result =
        failure(AssetLoadError(
            path,
            "LoadContext does not support raw asset dependency loading"
        ));
    if (const auto* context = dynamic_cast<const SyncLoadContext*>(this)) {
        result = context->read_asset_bytes_sync(path);
    } else if (
        const auto* context = dynamic_cast<const AsyncLoadContext*>(this)
    ) {
        result = context->read_asset_bytes_async(path);
    }
    if (result) {
        add_loader_dependency(path);
    }
    return result;
}

template<typename T>
Handle<T> LoadContext::add_asset(std::unique_ptr<T> asset) const {
    Handle<T> handle;
    if (auto* context = dynamic_cast<const SyncLoadContext*>(this)) {
        handle = context->template add_asset<T>(std::move(asset));
    } else if (auto* context = dynamic_cast<const AsyncLoadContext*>(this)) {
        handle = context->template add_asset<T>(std::move(asset));
    } else {
        throw std::runtime_error("LoadContext does not support asset creation");
    }
    add_dependency(AssetServer::asset_key(handle));
    return handle;
}

template<typename T>
Handle<T> SyncLoadContext::load(const AssetPath& path) const {
    return m_asset_server.template load<T>(path);
}

template<typename T>
Handle<T> SyncLoadContext::add_asset(std::unique_ptr<T> asset) const {
    return m_asset_server.template add_asset<T>(std::move(asset));
}

inline Result<std::vector<std::byte>, AssetLoadError>
SyncLoadContext::read_asset_bytes_sync(const AssetPath& path) const {
    return m_asset_server.read_asset_bytes(path);
}

template<typename T>
Handle<T> AsyncLoadContext::load(const AssetPath& path) const {
    if (!m_requests) {
        throw std::runtime_error(
            "Asset dependency request sender is not available"
        );
    }
    return m_requests->template load<T>(path);
}

template<typename T>
Handle<T> AsyncLoadContext::add_asset(std::unique_ptr<T> asset) const {
    if (!m_requests) {
        throw std::runtime_error(
            "Asset dependency request sender is not available"
        );
    }
    return m_requests->template add_asset<T>(std::move(asset));
}

inline Result<std::vector<std::byte>, AssetLoadError>
AsyncLoadContext::read_asset_bytes_async(const AssetPath& path) const {
    if (!m_requests) {
        return failure(AssetLoadError(
            path,
            "Asset dependency request sender is not available"
        ));
    }
    return m_requests->read_asset_bytes(path);
}

template<typename T>
Handle<T> AssetLoadRequestSender::load(const AssetPath& path) {
    struct Pending {
        std::mutex mutex;
        std::condition_variable completed;
        bool ready {false};
        Handle<T> result;
        std::string error;
    };

    auto pending = std::make_shared<Pending>();
    auto enqueued = enqueue(
        Request {
            .process =
                [pending, path](AssetServer& server) mutable {
                    auto result = server.template load_async<T>(path);
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
                        pending->error =
                            "Asset dependency request queue is closed for " +
                            path.as_string();
                        pending->ready = true;
                    }
                    pending->completed.notify_one();
                },
        }
    );
    if (!enqueued) {
        throw std::runtime_error(
            "Asset dependency request queue is closed for " + path.as_string()
        );
    }

    std::unique_lock lock(pending->mutex);
    pending->completed.wait(lock, [&]() {
        return pending->ready;
    });
    if (!pending->error.empty()) {
        throw std::runtime_error(pending->error);
    }
    return pending->result;
}

template<typename T>
Handle<T> AssetLoadRequestSender::add_asset(std::unique_ptr<T> asset) {
    struct Pending {
        std::mutex mutex;
        std::condition_variable completed;
        bool ready {false};
        Handle<T> result;
        std::string error;
    };

    auto pending = std::make_shared<Pending>();
    auto pending_asset = std::make_shared<std::unique_ptr<T>>(std::move(asset));
    auto enqueued = enqueue(
        Request {
            .process =
                [pending, pending_asset](AssetServer& server) mutable {
                    auto result =
                        server.template add_asset<T>(std::move(*pending_asset));
                    {
                        std::scoped_lock state_lock(pending->mutex);
                        pending->result = std::move(result);
                        pending->ready = true;
                    }
                    pending->completed.notify_one();
                },
            .cancel =
                [pending]() mutable {
                    {
                        std::scoped_lock state_lock(pending->mutex);
                        pending->error =
                            "Asset dependency request queue is closed";
                        pending->ready = true;
                    }
                    pending->completed.notify_one();
                },
        }
    );
    if (!enqueued) {
        throw std::runtime_error("Asset dependency request queue is closed");
    }

    std::unique_lock lock(pending->mutex);
    pending->completed.wait(lock, [&]() {
        return pending->ready;
    });
    if (!pending->error.empty()) {
        throw std::runtime_error(pending->error);
    }
    return pending->result;
}

inline Result<std::vector<std::byte>, AssetLoadError>
AssetLoadRequestSender::read_asset_bytes(const AssetPath& path) {
    struct Pending {
        std::mutex mutex;
        std::condition_variable completed;
        bool ready {false};
        std::vector<std::byte> bytes;
        Optional<AssetLoadError> error;
    };

    auto pending = std::make_shared<Pending>();
    auto enqueued = enqueue(
        Request {
            .process =
                [pending, path](AssetServer& server) mutable {
                    auto result = server.read_asset_bytes(path);
                    {
                        std::scoped_lock state_lock(pending->mutex);
                        if (result) {
                            pending->bytes = std::move(*result);
                        } else {
                            pending->error = std::move(result.error());
                        }
                        pending->ready = true;
                    }
                    pending->completed.notify_one();
                },
            .cancel =
                [pending, path]() mutable {
                    {
                        std::scoped_lock state_lock(pending->mutex);
                        pending->error = AssetLoadError(
                            path,
                            "Asset dependency request queue is closed for " +
                                path.as_string()
                        );
                        pending->ready = true;
                    }
                    pending->completed.notify_one();
                },
        }
    );
    if (!enqueued) {
        return failure(AssetLoadError(
            path,
            "Asset dependency request queue is closed for " + path.as_string()
        ));
    }

    std::unique_lock lock(pending->mutex);
    pending->completed.wait(lock, [&]() {
        return pending->ready;
    });
    if (pending->error) {
        return failure(std::move(*pending->error));
    }
    return std::move(pending->bytes);
}

template<typename T>
Handle<T> AssetLoadRequests::load(const AssetPath& path) {
    if (!m_sender) {
        throw std::runtime_error(
            "Asset dependency request sender is not available"
        );
    }
    return m_sender->template load<T>(path);
}

template<typename T>
Handle<T> AssetLoadRequests::add_asset(std::unique_ptr<T> asset) {
    if (!m_sender) {
        throw std::runtime_error(
            "Asset dependency request sender is not available"
        );
    }
    return m_sender->template add_asset<T>(std::move(asset));
}

} // namespace fei
