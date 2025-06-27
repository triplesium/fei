#pragma once

#include "core/transform.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics/buffer.hpp"
#include "graphics/program.hpp"
#include "math/primitives.hpp"
#include "render2d/camera.hpp"
#include "render2d/render.hpp"

#include <vector>

namespace fei {

extern const char* debug_vertex_shader;

extern const char* debug_fragment_shader;

struct Debug {
    std::vector<V2F_C4F> data;
    Program* program;
    Buffer* buffer;

    void line(Vector2 from, Vector2 to, Color4F color);
    void rect(Rect rect, Color4F color);
    void circle(
        Vector2 center,
        float radius,
        int segments,
        Color4F color = {1.0f, 1.0f, 1.0f, 1.0f}
    );
    void clear();
};

void setup_debug(Res<Debug> debug);

void draw_line_clear(Res<Debug> debug);

void draw_line_update(
    Res<Debug> debug,
    Res<RenderResource> render,
    Query<Camera, Transform2D> q_camera
);

class DebugPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
