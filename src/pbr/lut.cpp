#include "pbr/lut.hpp"

#include "app/app.hpp"
#include "core/image.hpp"
#include "ecs/system_config.hpp"
#include "rendering/plugin.hpp"

namespace fei {

void init_luts(Res<LUTs> luts, Res<AssetServer> asset_server) {
    luts->brdf_lut = asset_server->load<Image>("embeded://ibl_brdf_lut.png");
}

void init_gpu_luts(
    Res<LUTs> luts,
    Res<RenderAssets<GpuImage>> gpu_images,
    Res<GpuLUTs> gpu_luts
) {
    auto brdf_lut_gpu_image = gpu_images->get(luts->brdf_lut.id());
    if (!brdf_lut_gpu_image) {
        return;
    }
    gpu_luts->brdf_lut = *brdf_lut_gpu_image;
}

void LUTPlugin::setup(App& app) {
    app.add_resource(LUTs {})
        .add_resource(GpuLUTs {})
        .add_systems(StartUp, init_luts)
        .add_systems(
            RenderUpdate,
            init_gpu_luts | in_set<RenderingSystems::PrepareResources>()
        );
}

} // namespace fei
