#pragma once
#include "asset/io.hpp"
#include "asset/loader.hpp"
#include "rendering/mesh/mesh.hpp"

namespace fei {

class MeshLoader : public AssetLoader<Mesh> {
  public:
    AssetLoadResult<Mesh>
    load(Reader& reader, const LoadContext& context) override;
};

} // namespace fei
