#pragma once
#include "asset/handle.hpp"
#include "core/image.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/resource.hpp"
#include "graphics/sampler.hpp"
#include "graphics/shader_module.hpp"
#include "math/matrix.hpp"
#include "rendering/mesh/mesh.hpp"

#include <memory>
#include <vector>

namespace fei {

struct SkyboxResource {
    Handle<Mesh> mesh;
    std::vector<std::shared_ptr<const ShaderModule>> shader_modules;
    std::shared_ptr<ResourceLayout> resource_layout;
    std::shared_ptr<Sampler> sampler;
    std::shared_ptr<ResourceSet> resource_set;
    AssetId resource_set_image {invalid_asset_id};
    std::shared_ptr<Pipeline> pipeline;
};

struct alignas(16) SkyboxUniform {
    Matrix4x4 view_projection;
};

struct Skybox {
    Handle<Image> equirect_map;
};

class SkyboxPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
