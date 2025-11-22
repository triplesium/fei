#pragma once
#include <expected>
#include <filesystem>
#include <memory>
#include <system_error>

namespace fei {

template<typename T>
class AssetLoader {
  public:
    virtual std::expected<std::unique_ptr<T>, std::error_code>
    load(const std::filesystem::path& path) = 0;
};

} // namespace fei
