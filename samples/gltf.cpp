#include "gltf/gltf.hpp"

#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/plugin.hpp"
#include "asset/server.hpp"
#include "base/log.hpp"
#include "core/camera.hpp"
#include "core/image.hpp"
#include "core/time.hpp"
#include "core/transform.hpp"
#include "devtools/plugin.hpp"
#include "devtools_ecs/plugin.hpp"
#include "devtools_pbr/plugin.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "gltf/plugin.hpp"
#include "graphics_opengl_glfw/plugin.hpp"
#include "graphics_vulkan_glfw/plugin.hpp"
#include "input/input.hpp"
#include "math/vector.hpp"
#include "pbr/environment_map.hpp"
#include "pbr/light.hpp"
#include "pbr/plugin.hpp"
#include "pbr/skybox.hpp"
#include "rendering/plugin.hpp"
#include "scene/scene.hpp"
#include "window_glfw/input.hpp"

#include <cstdio>
#include <string_view>

using namespace ets;

namespace {

enum class GraphicsBackend {
    OpenGL,
    Vulkan,
};

struct Arguments {
    GraphicsBackend backend {GraphicsBackend::OpenGL};
    bool show_help {false};
};

struct PendingGltfScene {
    Handle<Gltf> gltf;
};

GraphicsBackend parse_backend(std::string_view value) {
    if (value == "opengl" || value == "gl") {
        return GraphicsBackend::OpenGL;
    }
    if (value == "vulkan" || value == "vk") {
        return GraphicsBackend::Vulkan;
    }
    fatal("sample-gltf --backend expects opengl or vulkan, got {}", value);
}

Arguments parse_arguments(int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument {argv[index]};
        if (argument == "--help" || argument == "-h") {
            result.show_help = true;
            continue;
        }
        if (argument == "--backend" || argument == "-b") {
            if (index + 1 >= argc) {
                fatal("sample-gltf {} requires a value", argument);
            }
            result.backend = parse_backend(argv[++index]);
            continue;
        }

        constexpr std::string_view backend_prefix {"--backend="};
        if (argument.starts_with(backend_prefix)) {
            result.backend =
                parse_backend(argument.substr(backend_prefix.size()));
            continue;
        }
        fatal("Unknown sample-gltf argument {}", argument);
    }
    return result;
}

void print_help() {
    std::puts("usage: sample-gltf [--backend=opengl|vulkan]");
    std::puts("       sample-gltf -b opengl");
    std::puts("       sample-gltf -b vulkan");
}

void add_graphics_backend(App& app, GraphicsBackend backend) {
    switch (backend) {
        case GraphicsBackend::OpenGL:
            app.add_plugin<OpenGLGlfwPlugin>();
            break;
        case GraphicsBackend::Vulkan:
            app.add_plugin<VulkanGlfwPlugin>();
            break;
    }
}

void setup(ResRW<AssetServer> asset_server, Commands commands) {
    commands.spawn().add(
        PendingGltfScene {
            .gltf = asset_server->load<Gltf>("models/WaterBottle.glb"),
        },
        Transform3d {}
    );

    commands.spawn().add(
        Camera3d {
            .fov_y = 45.0f,
            .near_plane = 0.01f,
            .far_plane = 100.0f,
        },
        Transform3d {
            .position = {0.0f, 0.1f, 0.4f},
        },
        GeneratedEquirectEnvironmentMap {
            .equirect_image = asset_server->load<Image>("autumn_field_4k.hdr"),
        },
        EnvironmentMapLight {
            .intensity = 0.5f,
        },
        Skybox {
            .equirect_map = asset_server->load<Image>("autumn_field_4k.hdr"),
        }
    );

    Transform3d light_transform;
    light_transform.set_euler({-45.0f, -35.0f, 0.0f});
    commands.spawn().add(
        DirectionalLight {
            .color = {1.0f, 1.0f, 1.0f},
            .intensity = 8.0f,
            .shadow_map_enabled = true,
        },
        light_transform
    );
}

void spawn_default_gltf_scene(
    Query<Entity, const PendingGltfScene> pending_scenes,
    ResRO<AssetServer> asset_server,
    ResRO<Assets<Gltf>> gltf_assets,
    Commands commands
) {
    for (const auto& [entity, pending] : pending_scenes) {
        const auto state = asset_server->load_state(pending.gltf);
        if (!state || *state == AssetLoadState::Loading) {
            continue;
        }
        if (*state == AssetLoadState::Failed) {
            const auto error = asset_server->load_error(pending.gltf);
            if (error) {
                ets::error(
                    "Failed to load '{}': {}",
                    error->path.as_string(),
                    error->message
                );
            } else {
                ets::error("Failed to load WaterBottle.glb");
            }
            commands.entity(entity).despawn();
            continue;
        }

        const auto gltf = gltf_assets->get(pending.gltf);
        if (!gltf) {
            continue;
        }
        if (!gltf->default_scene) {
            ets::error("WaterBottle.glb does not define a default scene");
            commands.entity(entity).despawn();
            continue;
        }
        if (*gltf->default_scene >= gltf->scenes.size()) {
            ets::error("WaterBottle.glb has an invalid default scene index");
            commands.entity(entity).despawn();
            continue;
        }

        commands.entity(entity).remove<PendingGltfScene>().add(
            SceneSpawner {
                .scene = gltf->scenes[*gltf->default_scene],
                .options = {},
            }
        );
    }
}

} // namespace

int main(int argc, char** argv) {
    const auto arguments = parse_arguments(argc, argv);
    if (arguments.show_help) {
        print_help();
        return 0;
    }

    App app;
    app.add_plugin<AssetsPlugin>().add_plugin<ImagePlugin>();
    add_graphics_backend(app, arguments.backend);
    app.add_plugin<RenderingPlugin>()
        .add_plugin<PbrPlugin>()
        .add_plugin<GlfwInputPlugin>()
        .add_plugin<TimePlugin>()
        .add_plugin<EnvironmentMapPlugin>()
        .add_plugin<GltfPlugin>()
        .add_systems(PreStartUp, setup)
        .add_systems(Update, spawn_default_gltf_scene);

    app.add_plugin(
        devtools::CorePlugin {devtools::Config {
            .host = "127.0.0.1",
            .port = 8080,
        }}
    );
    app.add_plugin(devtools::ecs::ProviderPlugin {});
    app.add_plugin(
        devtools::pbr::ProviderPlugin {devtools::pbr::Config {
            .jpeg_quality = 90,
        }}
    );

    app.run();
    return 0;
}
