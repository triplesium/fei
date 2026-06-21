#pragma once
#include "asset/io.hpp"
#include "base/result.hpp"

#include <filesystem>
#include <string>

namespace fei {

class AssetSource {
  public:
    virtual ~AssetSource() = default;
    virtual std::string name() const = 0;
    virtual bool exists(const std::filesystem::path& path) const = 0;
    virtual Result<Reader, std::string>
    try_get_reader(const std::filesystem::path& path) const = 0;

    Reader get_reader(const std::filesystem::path& path) const;
};

class DefaultAssetSource : public AssetSource {
  private:
    std::filesystem::path m_base_path;

  public:
    DefaultAssetSource();

    std::string name() const override;

    bool exists(const std::filesystem::path& path) const override;

    Result<Reader, std::string>
    try_get_reader(const std::filesystem::path& path) const override;
};

class EmbededAssetSource : public AssetSource {
  public:
    std::string name() const override;

    bool exists(const std::filesystem::path& path) const override;

    Result<Reader, std::string>
    try_get_reader(const std::filesystem::path& path) const override;
};

} // namespace fei
