#pragma once

#include "asset/loader.hpp"
#include "gltf/gltf.hpp"

namespace ets {

class GltfLoader : public AssetLoader<Gltf> {
  public:
    AssetLoadResult<Gltf>
    load(Reader& reader, const LoadContext& context) override;
};

} // namespace ets
