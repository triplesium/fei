#pragma once
#include "asset/io.hpp"
#include "asset/path.hpp"
#include "base/result.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ets {

enum class AssetEntryKind : std::uint8_t {
    File,
    Directory,
};

struct AssetEntry {
    AssetPath path;
    AssetEntryKind kind {AssetEntryKind::File};
    std::uintmax_t size {0};
    std::filesystem::file_time_type modified_at;
};

class AssetSource {
  public:
    virtual ~AssetSource() = default;
    virtual std::string name() const = 0;
    virtual bool exists(const std::filesystem::path& path) const = 0;
    virtual Result<Reader, std::string>
    try_get_reader(const std::filesystem::path& path) const = 0;

    virtual Result<std::vector<AssetEntry>, std::string>
    list(const std::filesystem::path& directory, bool recursive) const;

    Reader get_reader(const std::filesystem::path& path) const;
};

class FilesystemAssetSource : public AssetSource {
  private:
    std::string m_name;
    std::filesystem::path m_root;

    Result<std::filesystem::path, std::string>
    resolve(const std::filesystem::path& path) const;

  public:
    FilesystemAssetSource(std::string name, std::filesystem::path root);

    std::string name() const override;

    const std::filesystem::path& root() const { return m_root; }

    bool exists(const std::filesystem::path& path) const override;

    Result<Reader, std::string>
    try_get_reader(const std::filesystem::path& path) const override;

    Result<std::vector<AssetEntry>, std::string>
    list(const std::filesystem::path& directory, bool recursive) const override;
};

class EmbeddedAssetSource : public AssetSource {
  public:
    std::string name() const override;

    bool exists(const std::filesystem::path& path) const override;

    Result<Reader, std::string>
    try_get_reader(const std::filesystem::path& path) const override;
};

} // namespace ets
