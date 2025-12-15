#pragma once
#include "asset/loader.hpp"
#include "rendering/mesh.hpp"

#include <expected>
#include <memory>

namespace fei {

class MeshLoader : public AssetLoader<Mesh> {
  public:
    std::expected<std::unique_ptr<Mesh>, std::error_code>
    load(const std::filesystem::path& path) override;
};

} // namespace fei
