#pragma once
#include "asset/id.hpp"
#include "asset/io.hpp"
#include "asset/path.hpp"
#include "base/result.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace fei {

class AssetServer;
class AssetLoadRequestSender;
template<typename T>
class Handle;

struct AssetLoadError {
    AssetPath path;
    std::string message;

    AssetLoadError(AssetPath path, std::string message) :
        path(std::move(path)), message(std::move(message)) {}
};

template<typename T>
using AssetLoadResult = Result<std::unique_ptr<T>, AssetLoadError>;

class LoadContext {
  private:
    AssetPath m_asset_path;
    mutable std::vector<AssetKey> m_dependencies;

    void add_dependency(AssetKey dependency) const {
        m_dependencies.push_back(dependency);
    }

  public:
    explicit LoadContext(AssetPath asset_path) :
        m_asset_path(std::move(asset_path)) {}
    virtual ~LoadContext() = default;

    const AssetPath& asset_path() const { return m_asset_path; }
    const std::vector<AssetKey>& dependencies() const { return m_dependencies; }

    // Requests a dependency asset and returns its handle.
    //
    // With SyncLoadContext the dependency is loaded before this returns.
    // With AsyncLoadContext the dependency is scheduled on the asset task
    // system and the returned handle may still be in the Loading state.
    // Asset loaders should store dependency handles instead of dereferencing
    // dependency asset contents during async loading.
    template<typename T>
    Handle<T> load(const AssetPath& path) const;

    // Registers an asset produced while loading this asset and returns its
    // handle. The produced asset is recorded as a dependency of this load.
    template<typename T>
    Handle<T> add_asset(std::unique_ptr<T> asset) const;
};

class SyncLoadContext : public LoadContext {
  private:
    AssetServer& m_asset_server;

  public:
    SyncLoadContext(AssetServer& asset_server, AssetPath asset_path) :
        LoadContext(std::move(asset_path)), m_asset_server(asset_server) {}

    template<typename T>
    Handle<T> load(const AssetPath& path) const;

    template<typename T>
    Handle<T> add_asset(std::unique_ptr<T> asset) const;
};

class AsyncLoadContext : public LoadContext {
  private:
    std::shared_ptr<AssetLoadRequestSender> m_requests;

  public:
    AsyncLoadContext(
        std::shared_ptr<AssetLoadRequestSender> requests,
        AssetPath asset_path
    ) : LoadContext(std::move(asset_path)), m_requests(std::move(requests)) {}

    template<typename T>
    Handle<T> load(const AssetPath& path) const;

    template<typename T>
    Handle<T> add_asset(std::unique_ptr<T> asset) const;
};

template<typename T>
class AssetLoader {
  public:
    virtual ~AssetLoader() = default;
    virtual AssetLoadResult<T>
    load(Reader& reader, const LoadContext& context) = 0;
};

} // namespace fei
