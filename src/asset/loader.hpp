#pragma once
#include "asset/io.hpp"
#include "asset/path.hpp"

#include <expected>
#include <memory>
#include <system_error>
#include <utility>

namespace fei {

class AssetServer;
template<typename T>
class Handle;

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
};

template<typename T>
class AssetLoader {
  public:
    virtual ~AssetLoader() = default;
    virtual std::expected<std::unique_ptr<T>, std::error_code>
    load(Reader& reader, const LoadContext& context) = 0;
};

} // namespace fei
