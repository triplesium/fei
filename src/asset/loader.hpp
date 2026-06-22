#pragma once
#include "asset/io.hpp"
#include "asset/path.hpp"
#include "base/result.hpp"

#include <memory>
#include <string>
#include <utility>

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

  public:
    explicit LoadContext(AssetPath asset_path) :
        m_asset_path(std::move(asset_path)) {}
    virtual ~LoadContext() = default;

    const AssetPath& asset_path() const { return m_asset_path; }

    template<typename T>
    Handle<T> load(const AssetPath& path) const;

    template<typename T>
    Result<Handle<T>, AssetLoadError> try_load(const AssetPath& path) const;
};

class SyncLoadContext : public LoadContext {
  private:
    const AssetServer& m_asset_server;

  public:
    SyncLoadContext(const AssetServer& asset_server, AssetPath asset_path) :
        LoadContext(std::move(asset_path)), m_asset_server(asset_server) {}

    template<typename T>
    Handle<T> load(const AssetPath& path) const;

    template<typename T>
    Result<Handle<T>, AssetLoadError> try_load(const AssetPath& path) const;
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
    Result<Handle<T>, AssetLoadError> try_load(const AssetPath& path) const;
};

template<typename T>
class AssetLoader {
  public:
    virtual ~AssetLoader() = default;
    virtual AssetLoadResult<T>
    load(Reader& reader, const LoadContext& context) = 0;
};

} // namespace fei
