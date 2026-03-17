#include "app/app.hpp"
#include "asset/handle.hpp"
#include "asset/plugin.hpp"
#include "asset/server.hpp"
#include "core/camera.hpp"
#include "core/image.hpp"
#include "core/text.hpp"
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
#include "rendering/components.hpp"
#include "rendering/mesh/mesh.hpp"
#include "rendering/mesh/mesh_factory.hpp"
#include "rendering/plugin.hpp"
#include "rendering/shader.hpp"
#include "ui/plugin.hpp"
#include "window/input.hpp"
#include "window/window.hpp"

#include <glad/glad.h>
#include <glfw/glfw3.h>
#include <imgui.h>
#include <memory>
#include <print>

using namespace fei;

struct Foo {
    Handle<TextAsset> handle;
};

struct Spot {};
struct LightCube {};
struct Floor {};

class ColorOnlyMaterial : public StandardMaterial {
  public:
    ShaderRef fragment_shader() const override {
        return "embeded://color.frag";
    }
};

void setup(
    Res<AssetServer> asset_server,
    Commands commands,
    Res<Assets<Mesh>> mesh_assets,
    Res<Assets<StandardMaterial>> material_assets
) {
    auto spot_material = std::make_unique<StandardMaterial>();
    spot_material->albedo = {1.0f, 1.0f, 1.0f};
    // spot_material->albedo_map =
    //     asset_server->load<Image>("rustediron2_basecolor.png");
    spot_material->metallic = 1.0f;
    // spot_material->metallic_map =
    //     asset_server->load<Image>("rustediron2_metallic.png");
    spot_material->roughness = 0.2f;
    // spot_material->roughness_map =
    //     asset_server->load<Image>("rustediron2_roughness.png");
    // spot_material->normal_map =
    //     asset_server->load<Image>("rustediron2_normal.png");

    auto default_material = std::make_unique<StandardMaterial>();
    default_material->albedo = {0.8f, 0.8f, 0.8f};
    auto default_material_handle =
        material_assets->add(std::move(default_material));

    auto color_only_material = std::make_unique<ColorOnlyMaterial>();

    color_only_material->albedo = {1.0f, 1.0f, 1.0f};
    auto color_only_material_handle =
        material_assets->add(std::move(color_only_material));

    commands.spawn().add(
        Mesh3d {
            .mesh = mesh_assets->add(MeshFactory::create_plane(5.0f, 5.0f))
        },
        MeshMaterial3d {.material = default_material_handle},
        Transform3d {
            .position = {0.0f, -0.8f, 0.0f},
            .rotation = {0.0f, 0.0f, 0.0f},
        },
        Floor {}
    );

    auto mesh_handle = asset_server->load<Mesh>("suzanne.obj");
    auto& mesh = mesh_assets->get(mesh_handle).value();
    mesh.center_positions();
    if (!mesh.has_attribute(Mesh::ATTRIBUTE_NORMAL.id)) {
        mesh.compute_smooth_normals();
    }

    commands.spawn().add(
        Mesh3d {.mesh = mesh_handle},
        // Mesh3d {
        //     .mesh = mesh_assets->add(MeshFactory::create_sphere(0.5f, 64,
        //     64))
        // },
        MeshMaterial3d {
            .material = material_assets->add(std::move(spot_material))
        },
        Transform3d {
            // .scale = {6.0f, 6.0f, 6.0f},
        },
        Spot {}
    );

    commands.spawn().add(
        Camera3d {
            .fov_y = 45.0f,
            .near_plane = 0.1f,
            .far_plane = 100.0f,
        },
        Transform3d {
            .position = {0.0f, 0.0f, 5.0f},
        },
        GeneratedEquirectEnvironmentMap {
            .equirect_image = asset_server->load<Image>("autumn_field_4k.hdr"),
        }
    );

    commands.spawn().add(
        DirectionalLight {.color = {5.0f, 5.0f, 5.0f}},
        Transform3d {
            .position = {2.0f, 1.0f, 0.0f},
            .rotation = {-45.0f, 60.0f, 0.0f},
        }
    );
    commands.spawn().add(
        Mesh3d {
            .mesh = mesh_assets->add(
                MeshFactory::create_arrow(1.0f, 0.05f, 0.2f, 0.1f, 32)
            ),
            .cast_shadow = false,
        },
        MeshMaterial3d {.material = color_only_material_handle},
        Transform3d {},
        LightCube {}
    );
    commands.spawn().add(Skybox {
        .equirect_map = asset_server->load<Image>("autumn_field_4k.hdr"),
    });
}

void handle_control(
    Query<Transform3d>::Filter<With<Camera3d>> query,
    Res<KeyInput> key_input,
    Res<Time> time,
    Res<AppStates> app_states
) {
    auto [transform] = query.first();
    float move_speed = 1.0f;
    float rotate_speed = 20.0f;
    if (key_input->pressed(KeyCode::W)) {
        transform.position += transform.forward() * move_speed * time->delta();
    }
    if (key_input->pressed(KeyCode::S)) {
        transform.position -= transform.forward() * move_speed * time->delta();
    }
    if (key_input->pressed(KeyCode::A)) {
        transform.position -= transform.right() * move_speed * time->delta();
    }
    if (key_input->pressed(KeyCode::D)) {
        transform.position += transform.right() * move_speed * time->delta();
    }
    if (key_input->pressed(KeyCode::Up)) {
        transform.rotation.x += rotate_speed * time->delta();
    }
    if (key_input->pressed(KeyCode::Down)) {
        transform.rotation.x -= rotate_speed * time->delta();
    }
    if (key_input->pressed(KeyCode::Left)) {
        transform.rotation.y += rotate_speed * time->delta();
    }
    if (key_input->pressed(KeyCode::Right)) {
        transform.rotation.y -= rotate_speed * time->delta();
    }
    if (key_input->pressed(KeyCode::Space)) {
        transform.position.y += move_speed * time->delta();
    }
    if (key_input->pressed(KeyCode::LeftControl)) {
        transform.position.y -= move_speed * time->delta();
    }
    if (key_input->pressed(KeyCode::Escape)) {
        app_states->should_stop = true;
    }
}

void update_light_cube(
    Query<LightCube, Transform3d> query_cube,
    Query<DirectionalLight, Transform3d> query_light
) {
    auto [light_cube, cube_transform] = query_cube.first();
    auto [light, light_transform] = query_light.first();
    cube_transform.position = light_transform.position;
    cube_transform.rotation = light_transform.rotation;
}

void update_imgui(
    Query<DirectionalLight, Transform3d> query_light,
    Query<Floor, Transform3d> query_floor,
    Query<MeshMaterial3d<StandardMaterial>>::Filter<With<Spot>> query_spot,
    Res<Assets<StandardMaterial>> material_assets
) {
    auto [light, light_transform] = query_light.first();
    auto [floor, floor_transform] = query_floor.first();
    auto [spot_mesh_material] = query_spot.first();
    auto& spot_material =
        material_assets->get(spot_mesh_material.material).value();
    ImGui::Begin("Settings");
    {
        ImGui::Text("Directional Light");
        ImGui::SliderFloat3(
            "Position##Light",
            light_transform.position.data(),
            -10.0f,
            10.0f
        );
        ImGui::SliderFloat3(
            "Rotation##Light",
            light_transform.rotation.data(),
            -180.0f,
            180.0f
        );
        ImGui::Text("Floor");
        ImGui::SliderFloat(
            "Position.Y##Floor",
            &floor_transform.position.y,
            -1.0f,
            1.0f
        );
        ImGui::Text("Roughness");
        ImGui::SliderFloat("##Roughness", &spot_material.roughness, 0.0f, 1.0f);
    }
    ImGui::End();
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
        .add_plugin<UIPlugin>()
        .add_plugin<EnvironmentMapPlugin>()
        .add_systems(PreStartUp, setup)
        .add_systems(Update, handle_control, update_light_cube)
        .add_systems(
            RenderUpdate,
            update_imgui | in_set<RenderingSystems::Render>()
        )
        .run();

    return 0;
}
