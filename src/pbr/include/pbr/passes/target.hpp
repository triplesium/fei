#pragma once
#include "ecs/system_params.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/texture.hpp"
#include "window/window.hpp"

namespace fei {

struct RenderTarget {
    uint32 width {0};
    uint32 height {0};
    std::shared_ptr<Texture> color_texture;
    std::shared_ptr<Texture> depth_texture;
    std::shared_ptr<Texture> shadow_map_texture;

    [[nodiscard]] bool valid() const {
        return color_texture != nullptr && depth_texture != nullptr;
    }
};

struct DeferredViewTargets {
    uint32 width {0};
    uint32 height {0};
    std::shared_ptr<Texture> position_ao;
    std::shared_ptr<Texture> normal_roughness;
    std::shared_ptr<Texture> albedo_metallic;
    std::shared_ptr<Texture> specular;
    std::shared_ptr<Texture> emissive_depth;
    std::shared_ptr<Texture> direct;
    std::shared_ptr<Texture> indirect;
    std::shared_ptr<Texture> composite;

    [[nodiscard]] bool valid() const {
        return position_ao != nullptr && normal_roughness != nullptr &&
               albedo_metallic != nullptr && specular != nullptr &&
               emissive_depth != nullptr && direct != nullptr &&
               indirect != nullptr && composite != nullptr;
    }
};

void setup_render_target(
    ResRO<GraphicsDevice> device,
    ResRO<Window> window,
    ResRW<RenderTarget> target
);

void prepare_deferred_view_targets(
    ResRO<GraphicsDevice> device,
    ResRO<Window> window,
    ResRW<DeferredViewTargets> targets
);

} // namespace fei
