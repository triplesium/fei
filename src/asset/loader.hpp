#pragma once
#include "asset/io.hpp"
#include "asset/path.hpp"

#include <expected>
#include <memory>
#include <system_error>

namespace fei {

class LoadContext {
  private:
    AssetPath m_asset_path;

  public:
    LoadContext(const AssetPath& asset_path) : m_asset_path(asset_path) {}

    const AssetPath& asset_path() const { return m_asset_path; }
};

template<typename T>
class AssetLoader {
  public:
    virtual std::expected<std::unique_ptr<T>, std::error_code>
    load(Reader& reader, const LoadContext& context) = 0;
};

} // namespace fei
