#pragma once
#include "asset/handle.hpp"
#include "core/image.hpp"
#include "ecs/query.hpp"
#include "pbr/cubemap.hpp"
#include "rendering/gpu_image.hpp"

namespace fei {

struct GeneratedEquirectEnvironmentMap {
    Handle<Image> equirect_image;
};

struct EnvironmentMap {
    Handle<Image> environment_cubemap;
    Handle<Image> irradiance_cubemap;
    Handle<Image> radiance_cubemap;
};

struct GpuEnvironmentMap {
    GpuImage environment_cubemap;
    GpuImage irradiance_cubemap;
    GpuImage radiance_cubemap;
};

struct EnvironmentMapGeneratedTag {};

void generate_env_maps(
    Query<Entity, GpuEnvironmentMap>::Filter<
        Without<EnvironmentMapGeneratedTag>> query,
    Res<EquirectToCubemap> equirect_to_cubemap,
    Res<GraphicsDevice> device,
    Res<RenderAssets<GpuImage>> gpu_images,
    Res<AssetServer> asset_server,
    Res<Assets<Shader>> shaders,
    Commands commands
);

class EnvironmentMapPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
