#pragma once
#include <filesystem>

namespace fei {

template<typename T>
class AssetLoader {
  public:
    virtual T* load(const std::filesystem::path& path) = 0;
    virtual void unload(T* asset) final { delete asset; }
};

} // namespace fei
