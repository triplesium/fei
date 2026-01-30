#pragma once
#include "asset/io.hpp"
#include "asset/loader.hpp"
#include "rendering/mesh.hpp"

#include <expected>
#include <memory>

namespace fei {

class MeshLoader : public AssetLoader<Mesh> {
  public:
    std::expected<std::unique_ptr<Mesh>, std::error_code>
    load(Reader& reader, const LoadContext& context) override;
};

} // namespace fei
