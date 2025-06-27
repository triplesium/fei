#pragma once

#include "app/asset.hpp"
#include "core/transform.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics/buffer.hpp"
#include "graphics/program.hpp"
#include "graphics/render_pipeline.hpp"
#include "graphics/texture2d.hpp"
#include "render2d/camera.hpp"
#include "render2d/render.hpp"

namespace fei {

extern const char* sprite_vertex_shader;

extern const char* sprite_fragment_shader;

struct Sprite {
    Color4F color {1.0f, 1.0f, 1.0f, 1.0f};
    Vector2 anchor {0.5f, 0.5f};
    Handle<Texture2D> texture;
};

struct SpriteRendererResource {
    Buffer* vertex_buffer {nullptr};
    Program* program {nullptr};
    RenderPipeline* pipeline {nullptr};
};

void sprite_renderer_setup(Res<SpriteRendererResource> res);

void render_sprite(
    Res<SpriteRendererResource> sprite_res,
    Res<RenderResource> render_res,
    Res<AssetServer> asset_server,
    Query<Sprite, Transform2D> q_sprite,
    Query<Camera, Transform2D> q_camera
);

class SpritePlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
