#include "render_targets.hpp"

namespace fei::devtools::pbr {

const std::array<RenderTargetDescriptor, 8>& render_target_descriptors() {
    static const std::array descriptors {
        RenderTargetDescriptor {
            .id = "pbr.deferred.position_ao",
            .label = "Position / AO",
            .blob_capability = "",
            .expected_format = PixelFormat::Rgba16Float,
            .texture = &DeferredViewTargets::position_ao,
        },
        RenderTargetDescriptor {
            .id = "pbr.deferred.normal_roughness",
            .label = "Normal / Roughness",
            .blob_capability = "",
            .expected_format = PixelFormat::Rgba16Float,
            .texture = &DeferredViewTargets::normal_roughness,
        },
        RenderTargetDescriptor {
            .id = "pbr.deferred.albedo_metallic",
            .label = "Albedo / Metallic",
            .blob_capability = c_albedo_metallic_capability,
            .capture_name = "deferred_view_targets.albedo_metallic",
            .expected_format = PixelFormat::Rgba8Unorm,
            .texture = &DeferredViewTargets::albedo_metallic,
        },
        RenderTargetDescriptor {
            .id = "pbr.deferred.specular",
            .label = "Specular",
            .blob_capability = c_specular_capability,
            .capture_name = "deferred_view_targets.specular",
            .expected_format = PixelFormat::Rgba8Unorm,
            .texture = &DeferredViewTargets::specular,
        },
        RenderTargetDescriptor {
            .id = "pbr.deferred.emissive_depth",
            .label = "Emissive / Depth",
            .blob_capability = "",
            .expected_format = PixelFormat::Rgba16Float,
            .texture = &DeferredViewTargets::emissive_depth,
        },
        RenderTargetDescriptor {
            .id = "pbr.lighting.direct",
            .label = "Direct Lighting",
            .blob_capability = "",
            .expected_format = PixelFormat::Rgba16Float,
            .texture = &DeferredViewTargets::direct,
        },
        RenderTargetDescriptor {
            .id = "pbr.lighting.indirect",
            .label = "Indirect Lighting",
            .blob_capability = "",
            .expected_format = PixelFormat::Rgba16Float,
            .texture = &DeferredViewTargets::indirect,
        },
        RenderTargetDescriptor {
            .id = "pbr.deferred.composite",
            .label = "Composite",
            .blob_capability = c_composite_capability,
            .capture_name = "deferred_view_targets.composite",
            .expected_format = PixelFormat::Rgba8Unorm,
            .texture = &DeferredViewTargets::composite,
        },
    };
    return descriptors;
}

std::shared_ptr<Texture> resolve_render_target(
    const DeferredViewTargets& targets,
    const RenderTargetDescriptor& descriptor
) {
    return targets.*descriptor.texture;
}

bool is_directly_capturable(const std::shared_ptr<Texture>& texture) {
    return texture && texture->type() == TextureType::Texture2D &&
           texture->format() == PixelFormat::Rgba8Unorm &&
           texture->depth() == 1;
}

} // namespace fei::devtools::pbr
