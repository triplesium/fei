#include "app/app.hpp"
#include "asset/server.hpp"
#include "base/log.hpp"
#include "core/image.hpp"
#include "core/plugin.hpp"
#include "core/time.hpp"
#include "core/transform.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics_opengl_glfw/plugin.hpp"
#include "graphics_vulkan_glfw/plugin.hpp"
#include "graphics_webgpu_glfw/plugin.hpp"
#include "rendering/plugin.hpp"
#include "sprite/components.hpp"
#include "sprite/plugin.hpp"
#include "window/window.hpp"

#include <array>
#include <cstddef>
#include <string_view>

using namespace fei;

namespace {

enum class GraphicsBackend {
    OpenGL,
    Vulkan,
    WebGPU,
};

struct DemoSprite {};

GraphicsBackend parse_backend(std::string_view value) {
    if (value == "opengl" || value == "gl") {
        return GraphicsBackend::OpenGL;
    }
    if (value == "vulkan" || value == "vk") {
        return GraphicsBackend::Vulkan;
    }
    if (value == "webgpu" || value == "wgpu") {
        return GraphicsBackend::WebGPU;
    }
    fatal(
        "sample-sprite --backend expects opengl, vulkan, or webgpu; got {}",
        value
    );
}

GraphicsBackend parse_arguments(int argc, char** argv) {
    GraphicsBackend result = GraphicsBackend::OpenGL;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument {argv[index]};
        constexpr std::string_view prefix {"--backend="};
        if (argument.starts_with(prefix)) {
            result = parse_backend(argument.substr(prefix.size()));
            continue;
        }
        fatal("Unknown sample-sprite argument {}", argument);
    }
    return result;
}

void add_graphics_backend(App& app, GraphicsBackend backend) {
    switch (backend) {
        case GraphicsBackend::OpenGL:
            app.add_plugin<OpenGLGlfwPlugin>();
            break;
        case GraphicsBackend::Vulkan:
            app.add_plugin<VulkanGlfwPlugin>();
            break;
        case GraphicsBackend::WebGPU:
            app.add_plugin<WebGpuGlfwPlugin>();
            break;
    }
}

void setup_sprite_scene(ResRW<AssetServer> assets, Commands commands) {
    commands.spawn().add(
        Camera2d {
            .vertical_size = 6.0f,
            .clear_color = {0.05f, 0.07f, 0.1f, 1.0f},
        },
        Transform2d {}
    );
    const auto image = assets->load<Image>("awesomeface.png");
    const std::array regions {
        Rect {.min = {0.0f, 0.0f}, .max = {0.5f, 0.5f}},
        Rect {.min = {0.5f, 0.0f}, .max = {1.0f, 0.5f}},
        Rect {.min = {0.0f, 0.5f}, .max = {0.5f, 1.0f}},
        Rect {.min = {0.5f, 0.5f}, .max = {1.0f, 1.0f}},
    };
    for (std::size_t index = 0; index < regions.size(); ++index) {
        commands.spawn().add(
            Sprite {
                .image = image,
                .size = {1.8f, 1.8f},
                .uv_rect = regions[index],
                .flip_x = index == 1,
                .flip_y = index == 2,
            },
            Transform2d {
                .position =
                    {
                        -2.7f + static_cast<float>(index) * 1.8f,
                        0.0f,
                    },
            },
            DemoSprite {}
        );
    }
    commands.spawn().add(
        Sprite {
            .image = image,
            .size = {1.8f, 1.8f},
            .uv_rect = regions[0],
        },
        Transform2d {.position = {20.0f, 0.0f}},
        DemoSprite {}
    );
}

void animate_sprite(
    Query<Transform2d>::Filter<With<DemoSprite>> query,
    ResRO<Time> time
) {
    for (auto [transform] : query) {
        transform->rotation = time->elapsed_time() * 45.0f;
    }
}

} // namespace

int main(int argc, char** argv) {
    App app;
    app.add_resource(
        WindowConfig {
            .width = 800,
            .height = 450,
            .title = "Fei Sprite Sample",
        }
    );
    app.add_plugin<AssetsPlugin>();
    add_graphics_backend(app, parse_arguments(argc, argv));
    app.add_plugin<CorePlugin>()
        .add_plugin<RenderingPlugin>()
        .add_plugin<SpritePlugin>()
        .add_systems(PreStartUp, setup_sprite_scene)
        .add_systems(Update, animate_sprite)
        .run();
}
