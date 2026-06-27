#include "scene/scene.hpp"

#include "app/app.hpp"
#include "app/reflection_plugin.hpp"
#include "asset/plugin.hpp"
#include "asset/server.hpp"
#include "base/log.hpp"
#include "core/camera.hpp"
#include "core/fps_counter.hpp"
#include "core/image.hpp"
#include "core/time.hpp"
#include "core/transform.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics_opengl/plugin.hpp"
#include "math/vector.hpp"
#include "pbr/environment_map.hpp"
#include "pbr/light.hpp"
#include "pbr/material.hpp"
#include "pbr/plugin.hpp"
#include "pbr/skybox.hpp"
#include "pbr/vxgi.hpp"
#include "rendering/plugin.hpp"
#include "rendering/shader.hpp"
#include "scene/plugin.hpp"
#include "scripting/asset.hpp"
#include "scripting/script_system_registry.hpp"
#include "scripting_lua/plugin.hpp"
#include "scripting_lua/runtime.hpp"
#include "ui/plugin.hpp"
#include "web_preview/plugin.hpp"
#include "window/input.hpp"
#include "window/window.hpp"

#include <imgui.h>

using namespace fei;

class ColorOnlyMaterial : public StandardMaterial {
  public:
    ShaderRef fragment_shader() const override { return "shader://color.frag"; }
    ShaderRef deferred_fragment_shader() const override {
        return "shader://color.frag";
    }

    std::size_t hash() const override { return type_id<ColorOnlyMaterial>(); }
};

void setup(
    ResRW<AssetServer> asset_server,
    ResRO<Assets<ScriptAsset>> scripts,
    ResRW<LuaRuntime> runtime,
    ResRW<ScriptSystemRegistry> script_systems,
    Commands commands,
    WorldRef world
) {
    commands.spawn().add(
        SceneSpawner {
            .scene = asset_server->load<Scene>("sponza/sponza.obj"),
            .options = {.scale = Vector3 {0.01f}}
        }
    );

    Transform3d camera_transform {
        .position = {-4.0f, 1.0f, 0.0f},
    };
    camera_transform.set_euler({0.0f, 90.0f, 0.0f});

    commands.spawn().add(
        Camera3d {
            .fov_y = 45.0f,
            .near_plane = 0.1f,
            .far_plane = 10000.0f,
        },
        camera_transform,
        GeneratedEquirectEnvironmentMap {
            .equirect_image = asset_server->load<Image>("autumn_field_4k.hdr"),
        }
    );

    Transform3d directional_light_transform {
        .position = {1.0f, 35.6f, 1.3f},
    };
    directional_light_transform.set_euler({-83.14f, 7.30f, 0.0f});

    commands.spawn().add(
        DirectionalLight {
            .color = {1.0f, 1.0f, 1.0f},
            .intensity = 10.0f,
            .shadow_map_enabled = true,
        },
        directional_light_transform
    );
    commands.spawn().add(
        Skybox {
            .equirect_map = asset_server->load<Image>("autumn_field_4k.hdr"),
        }
    );

    auto camera_script = asset_server->load<ScriptAsset>("camera_control.lua");
    auto loaded_script_system =
        script_systems->load_asset(*runtime, *world, *scripts, camera_script);
    if (!loaded_script_system) {
        error(
            "Failed to load camera control script system: {}",
            loaded_script_system.error().message
        );
    }
}

void update_directional_light(
    Query<DirectionalLight, Transform3d> query_directional_lights,
    ResRO<VxgiVoxelization> voxelization
) {
    auto& aabb = voxelization->scene_aabb;
    for (auto [light, transform] : query_directional_lights) {
        light.projection_size = aabb.extent().magnitude() * 2.0f;
        transform.position =
            aabb.center() - transform.forward() * aabb.extent().magnitude();
    }
}

void update_imgui(
    Query<DirectionalLight, Transform3d> query_directional_lights,
    Query<PointLight, Transform3d> query_point_lights,
    ResRO<FpsCounter> fps_counter
) {
    ImGui::Begin("FPS");
    ImGui::Text("FPS: %.2f", fps_counter->fps);
    ImGui::Text(
        "Frame Time: %.2f ms",
        fps_counter->frame_time_seconds * 1000.0f
    );
    ImGui::End();

    for (const auto& [light, transform] : query_directional_lights) {
        ImGui::Begin("Directional Light");
        ImGui::ColorEdit3("Color", reinterpret_cast<float*>(&light.color));
        ImGui::DragFloat("Intensity", &light.intensity, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat3(
            "Position",
            reinterpret_cast<float*>(&transform.position),
            0.1f
        );
        static Vector3 rotation {-83.14f, 7.30f, 0.0f};
        if (ImGui::DragFloat3("Rotation", rotation.data(), 0.1f)) {
            transform.set_euler(rotation);
        }
        ImGui::End();
    }
    for (const auto& [light, transform] : query_point_lights) {
        ImGui::Begin("Point Light");
        ImGui::ColorEdit3("Color", reinterpret_cast<float*>(&light.color));
        ImGui::DragFloat("Intensity", &light.intensity, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("Range", &light.range, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat3(
            "Position",
            reinterpret_cast<float*>(&transform.position),
            0.1f
        );
        ImGui::End();
    }
}

int main() {
    App()
        .add_plugin<WindowPlugin>()
        .add_plugin<AssetsPlugin>()
        .add_plugin<ImagePlugin>()
        .add_plugin<OpenGLPlugin>()
        .add_plugin<RenderingPlugin>()
        .add_plugin<PbrPlugin>()
        .add_plugin<InputPlugin>()
        .add_plugin<TimePlugin>()
        .add_plugin<FpsCounterPlugin>()
        .add_plugin<UIPlugin>()
        .add_plugin<EnvironmentMapPlugin>()
        .add_plugin<ScenePlugin>()
        .add_plugin<ReflectionPlugin>()
        .add_plugin<LuaScriptingPlugin>()
        .add_systems(PreStartUp, setup)
        .add_systems(Update, update_directional_light)
        .add_systems(
            RenderUpdate,
            update_imgui | in_set<RenderingSystems::Render>() | main_thread()
        )
        .add_plugin(
            WebPreviewPlugin {WebPreviewConfig {
                .host = "127.0.0.1",
                .port = 8080,
                .jpeg_quality = 80,
            }}
        )
        .run();

    return 0;
}
