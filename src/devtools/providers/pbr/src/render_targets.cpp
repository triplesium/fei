#include "render_targets.hpp"

namespace fei::devtools::pbr {

const char* preview_mode_name(PreviewMode mode) {
    switch (mode) {
        case PreviewMode::Color:
            return "color";
        case PreviewMode::ScalarAlpha:
            return "scalar_alpha";
        case PreviewMode::Normal:
            return "normal";
        case PreviewMode::Position:
            return "position";
        case PreviewMode::Depth:
            return "depth";
        case PreviewMode::ToneMappedColor:
            return "tone_mapped_color";
    }
    return "unknown";
}

const std::array<RenderTargetDescriptor, 8>& render_target_descriptors() {
    static const std::array position_views {
        RenderTargetViewDescriptor {
            .id = "position",
            .label = "Position",
            .blob_capability = c_position_capability,
            .mode = PreviewMode::Position,
        },
    };
    static const std::array normal_roughness_views {
        RenderTargetViewDescriptor {
            .id = "normal",
            .label = "Normal",
            .blob_capability = c_normal_capability,
            .mode = PreviewMode::Normal,
        },
        RenderTargetViewDescriptor {
            .id = "roughness",
            .label = "Roughness",
            .blob_capability = c_roughness_capability,
            .mode = PreviewMode::ScalarAlpha,
        },
    };
    static const std::array albedo_metallic_views {
        RenderTargetViewDescriptor {
            .id = "albedo",
            .label = "Albedo",
            .blob_capability = c_albedo_capability,
            .mode = PreviewMode::Color,
        },
        RenderTargetViewDescriptor {
            .id = "metallic",
            .label = "Metallic",
            .blob_capability = c_metallic_capability,
            .mode = PreviewMode::ScalarAlpha,
        },
    };
    static const std::array specular_views {
        RenderTargetViewDescriptor {
            .id = "specular",
            .label = "Specular",
            .blob_capability = c_specular_capability,
            .mode = PreviewMode::Color,
        },
    };
    static const std::array emissive_depth_views {
        RenderTargetViewDescriptor {
            .id = "emissive",
            .label = "Emissive",
            .blob_capability = c_emissive_capability,
            .mode = PreviewMode::Color,
        },
        RenderTargetViewDescriptor {
            .id = "depth",
            .label = "Depth",
            .blob_capability = c_depth_capability,
            .mode = PreviewMode::Depth,
        },
    };
    static const std::array direct_views {
        RenderTargetViewDescriptor {
            .id = "direct",
            .label = "Direct Lighting",
            .blob_capability = c_direct_capability,
            .mode = PreviewMode::ToneMappedColor,
            .geometry_mask = false,
        },
    };
    static const std::array indirect_views {
        RenderTargetViewDescriptor {
            .id = "indirect",
            .label = "Indirect Lighting",
            .blob_capability = c_indirect_capability,
            .mode = PreviewMode::ToneMappedColor,
            .geometry_mask = false,
        },
    };
    static const std::array composite_views {
        RenderTargetViewDescriptor {
            .id = "composite",
            .label = "Rendered Frame",
            .blob_capability = c_composite_capability,
            .mode = PreviewMode::Color,
            .geometry_mask = false,
        },
    };
    static const std::array descriptors {
        RenderTargetDescriptor {
            .id = "pbr.deferred.position_ao",
            .label = "Position / AO",
            .capture_name = "deferred_view_targets.position_ao",
            .expected_format = PixelFormat::Rgba16Float,
            .texture = &DeferredViewTargets::position_ao,
            .views = position_views,
        },
        RenderTargetDescriptor {
            .id = "pbr.deferred.normal_roughness",
            .label = "Normal / Roughness",
            .capture_name = "deferred_view_targets.normal_roughness",
            .expected_format = PixelFormat::Rgba16Float,
            .texture = &DeferredViewTargets::normal_roughness,
            .views = normal_roughness_views,
        },
        RenderTargetDescriptor {
            .id = "pbr.deferred.albedo_metallic",
            .label = "Albedo / Metallic",
            .capture_name = "deferred_view_targets.albedo_metallic",
            .expected_format = PixelFormat::Rgba8Unorm,
            .texture = &DeferredViewTargets::albedo_metallic,
            .views = albedo_metallic_views,
        },
        RenderTargetDescriptor {
            .id = "pbr.deferred.specular",
            .label = "Specular",
            .capture_name = "deferred_view_targets.specular",
            .expected_format = PixelFormat::Rgba8Unorm,
            .texture = &DeferredViewTargets::specular,
            .views = specular_views,
        },
        RenderTargetDescriptor {
            .id = "pbr.deferred.emissive_depth",
            .label = "Emissive / Depth",
            .capture_name = "deferred_view_targets.emissive_depth",
            .expected_format = PixelFormat::Rgba16Float,
            .texture = &DeferredViewTargets::emissive_depth,
            .views = emissive_depth_views,
        },
        RenderTargetDescriptor {
            .id = "pbr.lighting.direct",
            .label = "Direct Lighting",
            .capture_name = "deferred_view_targets.direct",
            .expected_format = PixelFormat::Rgba16Float,
            .texture = &DeferredViewTargets::direct,
            .views = direct_views,
        },
        RenderTargetDescriptor {
            .id = "pbr.lighting.indirect",
            .label = "Indirect Lighting",
            .capture_name = "deferred_view_targets.indirect",
            .expected_format = PixelFormat::Rgba16Float,
            .texture = &DeferredViewTargets::indirect,
            .views = indirect_views,
        },
        RenderTargetDescriptor {
            .id = "pbr.deferred.composite",
            .label = "Composite",
            .capture_name = "deferred_view_targets.composite",
            .expected_format = PixelFormat::Rgba8Unorm,
            .texture = &DeferredViewTargets::composite,
            .views = composite_views,
        },
    };
    return descriptors;
}

std::size_t render_target_view_count() {
    std::size_t count = 0;
    for (const auto& descriptor : render_target_descriptors()) {
        count += descriptor.views.size();
    }
    return count;
}

RenderTargetViewRef render_target_view_at(std::size_t index) {
    for (const auto& descriptor : render_target_descriptors()) {
        if (index < descriptor.views.size()) {
            return {
                .target = &descriptor,
                .view = &descriptor.views[index],
            };
        }
        index -= descriptor.views.size();
    }
    return {};
}

std::shared_ptr<Texture> resolve_render_target(
    const DeferredViewTargets& targets,
    const RenderTargetDescriptor& descriptor
) {
    return targets.*descriptor.texture;
}

bool is_previewable(const std::shared_ptr<Texture>& texture) {
    return texture && texture->type() == TextureType::Texture2D &&
           texture->depth() == 1 &&
           texture->usage().is_set(TextureUsage::Sampled) &&
           (texture->format() == PixelFormat::Rgba8Unorm ||
            texture->format() == PixelFormat::Rgba16Float);
}

} // namespace fei::devtools::pbr
