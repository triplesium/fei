#include "rendering/defaults.hpp"

#include "app/app.hpp"
#include "graphics/graphics_device.hpp"
#include "math/color.hpp"

namespace fei {

void init_rendering_defaults(
    Res<GraphicsDevice> device,
    Res<RenderingDefaults> defaults
) {
    defaults->default_texture = device->create_texture(TextureDescription {
        .width = 1,
        .height = 1,
        .depth = 1,
        .mip_level = 1,
        .layer = 1,
        .texture_format = PixelFormat::Rgba8Unorm,
        .texture_usage = TextureUsage::Sampled,
        .texture_type = TextureType::Texture2D,
    });
    auto data = Color4B {255, 255, 255, 255};
    device->update_texture(
        defaults->default_texture,
        &data,
        0,
        0,
        0,
        1,
        1,
        1,
        0,
        0
    );
}

void RenderingDefaultsPlugin::setup(App& app) {
    app.add_resource(RenderingDefaults {})
        .add_systems(StartUp, init_rendering_defaults);
}

} // namespace fei
