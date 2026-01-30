#pragma once
#include "asset/embed.hpp"
#include "asset/io.hpp"

#include <string>

namespace fei {

class AssetSource {
  public:
    virtual ~AssetSource() = default;
    virtual std::string name() const = 0;
    virtual bool exists(const std::filesystem::path& path) const = 0;
    virtual Reader get_reader(const std::filesystem::path& path) const = 0;
};

class DefaultAssetSource : public AssetSource {
  private:
    std::filesystem::path m_base_path;

  public:
    DefaultAssetSource() {
#ifdef FEI_ASSETS_PATH
        m_base_path = FEI_ASSETS_PATH;
#else
        m_base_path = std::filesystem::current_path();
#endif
    }

    virtual std::string name() const override { return "default"; }

    virtual bool exists(const std::filesystem::path& path) const override {
        return std::filesystem::exists(m_base_path / path);
    }

    virtual Reader get_reader(const std::filesystem::path& path
    ) const override {
        return Reader(m_base_path / path);
    }
};

class EmbededAssetSource : public AssetSource {
  public:
    virtual std::string name() const override { return "embeded"; }

    virtual bool exists(const std::filesystem::path& path) const override {
        return EmbededAssets::has(path.string());
    }

    virtual Reader get_reader(const std::filesystem::path& path
    ) const override {
        return EmbededAssets::get(path.string()).reader();
    }
};

} // namespace fei
