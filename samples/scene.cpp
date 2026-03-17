#include "scene/scene.hpp"

#include "app/app.hpp"
#include "asset/handle.hpp"
#include "asset/plugin.hpp"
#include "asset/server.hpp"
#include "core/camera.hpp"
#include "core/fps_counter.hpp"
#include "core/image.hpp"
#include "core/time.hpp"
#include "core/transform.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics/opengl/plugin.hpp"
#include "math/vector.hpp"
#include "pbr/environment_map.hpp"
#include "pbr/light.hpp"
#include "pbr/material.hpp"
#include "pbr/plugin.hpp"
#include "pbr/skybox.hpp"
#include "pbr/vxgi.hpp"
#include "rendering/mesh/mesh.hpp"
#include "rendering/plugin.hpp"
#include "rendering/shader.hpp"
#include "scene/plugin.hpp"
#include "scripting/component.hpp"
#include "scripting/plugin.hpp"
#include "ui/plugin.hpp"
#include "window/input.hpp"
#include "window/window.hpp"

#include <imgui.h>
#include <print>

using namespace fei;

class ColorOnlyMaterial : public StandardMaterial {
  public:
    ShaderRef fragment_shader() const override {
        return "embeded://color.frag";
    }
    ShaderRef deferred_fragment_shader() const override {
        return "embeded://color.frag";
    }

    std::size_t hash() const override { return type_id<ColorOnlyMaterial>(); }
};

void setup(
    Res<AssetServer> asset_server,
    Commands commands,
    Res<Assets<Mesh>> mesh_assets,
    Res<Assets<StandardMaterial>> material_assets
) {
    commands.spawn().add(SceneSpawner {
        .scene = asset_server->load<Scene>("sponza/sponza.obj"),
        .options = {.scale = Vector3 {0.01f}}
    });

    commands.spawn().add(
        Camera3d {
            .fov_y = 45.0f,
            .near_plane = 0.1f,
            .far_plane = 10000.0f,
        },
        Transform3d {
            .position = {-4.0f, 1.0f, 0.0f},
            .rotation = {0.0f, 90.0f, 0.0f},
        },
        GeneratedEquirectEnvironmentMap {
            .equirect_image = asset_server->load<Image>("autumn_field_4k.hdr"),
        },
        ScriptComponent {
            .script = asset_server->load<ScriptAsset>("camera_control.lua"),
        }
    );

    commands.spawn().add(
        DirectionalLight {
            .color = {1.0f, 1.0f, 1.0f},
            .intensity = 10.0f,
            .shadow_map_enabled = true,
        },
        Transform3d {
            .position = {1.0f, 35.6f, 1.3f},
            .rotation = {-80.0f, 47.0f, 0.0f},
        }
    );
    commands.spawn().add(Skybox {
        .equirect_map = asset_server->load<Image>("autumn_field_4k.hdr"),
    });
}

void update_directional_light(
    Query<DirectionalLight, Transform3d> query_directional_lights,
    Res<VxgiVoxelization> voxelization
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
    Res<FpsCounter> fps_counter
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
        ImGui::DragFloat3(
            "Rotation",
            reinterpret_cast<float*>(&transform.rotation),
            0.1f
        );
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
        .add_plugin<VxgiPlugin>()
        .add_plugin<ScriptingPlugin>()
        .add_systems(PreStartUp, setup)
        .add_systems(Update, update_directional_light)
        .add_systems(
            RenderUpdate,
            update_imgui | in_set<RenderingSystems::Render>()
        )
        .run();

    return 0;
}
