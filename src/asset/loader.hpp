#pragma once
#include "asset/io.hpp"
#include "asset/path.hpp"
#include "base/result.hpp"

#include <memory>
#include <string>
#include <utility>

namespace fei {

class AssetServer;
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
    const AssetServer& m_asset_server;
    AssetPath m_asset_path;

  public:
    LoadContext(const AssetServer& asset_server, AssetPath asset_path) :
        m_asset_server(asset_server), m_asset_path(std::move(asset_path)) {}

    const AssetPath& asset_path() const { return m_asset_path; }

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
