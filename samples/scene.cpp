#include "scene/scene.hpp"

#include "app/app.hpp"
#include "app/reflection_plugin.hpp"
#include "asset/plugin.hpp"
#include "asset/server.hpp"
#include "base/log.hpp"
#include "core/camera.hpp"
#include "core/image.hpp"
#include "core/time.hpp"
#include "core/transform.hpp"
#include "devtools/plugin.hpp"
#include "devtools_ecs/plugin.hpp"
#include "devtools_input/plugin.hpp"
#include "devtools_pbr/plugin.hpp"
#include "devtools_profiling/plugin.hpp"
#include "devtools_reflection/plugin.hpp"
#include "devtools_rendering/plugin.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics_opengl_glfw/plugin.hpp"
#include "graphics_vulkan_glfw/plugin.hpp"
#include "imgui/plugin.hpp"
#include "math/vector.hpp"
#include "pbr/environment_map.hpp"
#include "pbr/light.hpp"
#include "pbr/material.hpp"
#include "pbr/plugin.hpp"
#include "pbr/skybox.hpp"
#include "pbr/vxgi.hpp"
#include "profiling/profiling.hpp"
#include "rendering/plugin.hpp"
#include "rendering/shader.hpp"
#include "scene/plugin.hpp"
#include "scripting_lua/asset.hpp"
#include "scripting_lua/plugin.hpp"
#include "scripting_lua/script_system_registry.hpp"
#include "window/input.hpp"

#include <cstdio>
#include <imgui.h>
#include <string_view>

using namespace fei;

namespace {

enum class SceneGraphicsBackend {
    OpenGL,
    Vulkan,
};

struct SceneArguments {
    SceneGraphicsBackend backend {SceneGraphicsBackend::OpenGL};
    bool show_help {false};
};

struct DirectionalLightUiState {
    Vector3 rotation;
};

SceneGraphicsBackend default_scene_backend() {
    return SceneGraphicsBackend::OpenGL;
}

std::string_view backend_name(SceneGraphicsBackend backend) {
    switch (backend) {
        case SceneGraphicsBackend::OpenGL:
            return "opengl";
        case SceneGraphicsBackend::Vulkan:
            return "vulkan";
    }
    return "unknown";
}

SceneGraphicsBackend parse_backend(std::string_view value) {
    if (value == "opengl" || value == "gl") {
        return SceneGraphicsBackend::OpenGL;
    }
    if (value == "vulkan" || value == "vk") {
        return SceneGraphicsBackend::Vulkan;
    }
    fatal("sample-scene --backend expects opengl or vulkan, got {}", value);
}

SceneArguments parse_scene_arguments(int argc, char** argv) {
    SceneArguments result {
        .backend = default_scene_backend(),
    };
    for (int index = 1; index < argc; ++index) {
        const std::string_view arg {argv[index]};
        if (arg == "--help" || arg == "-h") {
            result.show_help = true;
            continue;
        }
        if (arg == "--backend" || arg == "-b") {
            if (index + 1 >= argc) {
                fatal("sample-scene {} requires a value", arg);
            }
            result.backend = parse_backend(argv[++index]);
            continue;
        }

        constexpr std::string_view backend_prefix {"--backend="};
        if (arg.starts_with(backend_prefix)) {
            result.backend = parse_backend(arg.substr(backend_prefix.size()));
            continue;
        }

        fatal("Unknown sample-scene argument {}", arg);
    }
    return result;
}

void print_help() {
    std::puts("usage: sample-scene [--backend=opengl|vulkan]");
    std::puts("       sample-scene -b opengl");
    std::puts("       sample-scene -b vulkan");
}

void add_graphics_backend(App& app, SceneGraphicsBackend backend) {
    switch (backend) {
        case SceneGraphicsBackend::OpenGL:
            app.add_plugin<OpenGLGlfwPlugin>();
            break;
        case SceneGraphicsBackend::Vulkan:
            app.add_plugin<VulkanGlfwPlugin>();
            break;
    }
}

float percent(uint64 value, uint64 total) {
    if (total == 0) {
        return 0.0f;
    }
    return static_cast<float>(value) * 100.0f / static_cast<float>(total);
}

void draw_render_schedule_stats(const Optional<ScheduleDebugInfo>& debug) {
    if (!debug) {
        ImGui::TextUnformatted("Render schedule unavailable");
        return;
    }
    ImGui::Text(
        "Render schedule: %zu systems, %zu batches",
        debug->systems.size(),
        debug->batches.size()
    );
}

void draw_graphics_cache_stats(const GraphicsDevice& device) {
    const auto stats = device.resource_cache_stats();
    ImGui::Text(
        "Framebuffers: %llu req, %llu hit, %llu create (%.1f%%), cache %zu",
        static_cast<unsigned long long>(stats.framebuffer_requests),
        static_cast<unsigned long long>(stats.framebuffer_hits),
        static_cast<unsigned long long>(stats.framebuffer_creates),
        percent(stats.framebuffer_hits, stats.framebuffer_requests),
        stats.framebuffer_cache_size
    );
    ImGui::Text(
        "Resource sets: %llu req, %llu hit, %llu create (%.1f%%), cache %zu",
        static_cast<unsigned long long>(stats.resource_set_requests),
        static_cast<unsigned long long>(stats.resource_set_hits),
        static_cast<unsigned long long>(stats.resource_set_creates),
        percent(stats.resource_set_hits, stats.resource_set_requests),
        stats.resource_set_cache_size
    );
}

using DirectionalLightQuery =
    Query<Entity, DirectionalLight, Transform3d, DirectionalLightUiState>;
using PointLightQuery = Query<Entity, PointLight, Transform3d>;
using EnvironmentLightQuery = Query<Entity, EnvironmentMapLight>;

void draw_lighting_controls(
    DirectionalLightQuery& query_directional_lights,
    PointLightQuery& query_point_lights
) {
    if (ImGui::CollapsingHeader(
            "Directional Lights",
            ImGuiTreeNodeFlags_DefaultOpen
        )) {
        if (query_directional_lights.empty()) {
            ImGui::TextDisabled("No directional lights");
        }
        for (auto
             [entity,
              light_component,
              transform_component,
              ui_state_component] : query_directional_lights) {
            ImGui::PushID(static_cast<int>(entity));
            if (ImGui::TreeNodeEx(
                    "Directional Light",
                    ImGuiTreeNodeFlags_DefaultOpen
                )) {
                auto& light = light_component.write();
                auto& transform = transform_component.write();
                auto& ui_state = ui_state_component.write();
                ImGui::ColorEdit3(
                    "Color",
                    reinterpret_cast<float*>(&light.color)
                );
                ImGui::DragFloat(
                    "Intensity",
                    &light.intensity,
                    0.1f,
                    0.0f,
                    100.0f
                );
                if (ImGui::DragFloat3(
                        "Rotation",
                        ui_state.rotation.data(),
                        0.1f
                    )) {
                    transform.set_euler(ui_state.rotation);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Point Lights")) {
        if (query_point_lights.empty()) {
            ImGui::TextDisabled("No point lights");
        }
        for (auto [entity, light_component, transform_component] :
             query_point_lights) {
            ImGui::PushID(static_cast<int>(entity));
            if (ImGui::TreeNode("Point Light")) {
                auto& light = light_component.write();
                auto& transform = transform_component.write();
                ImGui::ColorEdit3(
                    "Color",
                    reinterpret_cast<float*>(&light.color)
                );
                ImGui::DragFloat(
                    "Intensity",
                    &light.intensity,
                    0.1f,
                    0.0f,
                    100.0f
                );
                ImGui::DragFloat("Range", &light.range, 0.1f, 0.0f, 100.0f);
                ImGui::DragFloat3(
                    "Position",
                    reinterpret_cast<float*>(&transform.position),
                    0.1f
                );
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
}

void draw_indirect_lighting_controls(
    EnvironmentLightQuery& query_environment_lights,
    VxgiVolumes& vxgi_volumes
) {
    if (ImGui::CollapsingHeader(
            "Environment / IBL",
            ImGuiTreeNodeFlags_DefaultOpen
        )) {
        if (query_environment_lights.empty()) {
            ImGui::TextDisabled("No environment map lights");
        }
        for (auto [entity, light_component] : query_environment_lights) {
            ImGui::PushID(static_cast<int>(entity));
            auto& light = light_component.write();
            ImGui::Checkbox("Enable IBL", &light.enabled);
            ImGui::BeginDisabled(!light.enabled);
            ImGui::DragFloat("Intensity", &light.intensity, 0.01f, 0.0f, 10.0f);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("VXGI", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat(
            "Bounce Strength",
            &vxgi_volumes.config.bounce_strength,
            0.01f,
            0.0f,
            2.0f
        );
        ImGui::DragFloat(
            "Skylight Leaking",
            &vxgi_volumes.config.skylight_leaking,
            0.01f,
            0.0f,
            1.0f
        );
    }
}

void draw_diagnostics(const GraphicsDevice& device, World& world) {
    if (ImGui::CollapsingHeader(
            "Diagnostics",
            ImGuiTreeNodeFlags_DefaultOpen
        )) {
        const auto frame_stats = profile_frame_stats();
        ImGui::Text(
            "FPS: %.2f    Frame Time: %.2f ms",
            frame_stats.fps,
            frame_stats.latest_frame_ms
        );
        draw_render_schedule_stats(world.schedule_debug_info(RenderUpdate));
        if (ImGui::TreeNode("Graphics Cache")) {
            draw_graphics_cache_stats(device);
            ImGui::TreePop();
        }
    }
}

} // namespace

class ColorOnlyMaterial : public StandardMaterial {
  public:
    ShaderRef fragment_shader() const override {
        return "shader://pbr/color.slang";
    }

    std::size_t hash() const override { return type_id<ColorOnlyMaterial>(); }
};

void setup(
    ResRW<AssetServer> asset_server,
    ResRW<LuaScriptSystemRegistry> lua_scripts,
    Commands commands
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
        },
        EnvironmentMapLight {
            .intensity = 0.5f,
        },
        Skybox {
            .equirect_map = asset_server->load<Image>("autumn_field_4k.hdr"),
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
        directional_light_transform,
        DirectionalLightUiState {
            .rotation = {-83.14f, 7.30f, 0.0f},
        }
    );
    auto camera_script =
        asset_server->load<LuaScriptAsset>("camera_control.lua");
    lua_scripts->queue_asset(camera_script);
}

void configure_vxgi(ResRW<VxgiVolumes> volumes) {
    volumes->config.voxel_resolution = 128;
    volumes->config.bounce_strength = 1.0f;
}

void update_directional_light(
    Query<DirectionalLight, Transform3d> query_directional_lights,
    ResRO<VxgiVoxelization> voxelization
) {
    auto& aabb = voxelization->scene_aabb;
    for (auto [light, transform] : query_directional_lights) {
        light->projection_size = aabb.extent().magnitude() * 2.0f;
        auto& transform_value = transform.write();
        transform_value.position =
            aabb.center() -
            transform_value.forward() * aabb.extent().magnitude();
    }
}

void update_imgui(
    DirectionalLightQuery query_directional_lights,
    PointLightQuery query_point_lights,
    EnvironmentLightQuery query_environment_lights,
    ResRW<VxgiVolumes> vxgi_volumes,
    ResRO<GraphicsDevice> graphics_device,
    WorldRef world
) {
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Scene Controls")) {
        draw_diagnostics(*graphics_device, *world);
        ImGui::Separator();
        if (ImGui::BeginTabBar("SceneControlsTabs")) {
            if (ImGui::BeginTabItem("Lighting")) {
                draw_lighting_controls(
                    query_directional_lights,
                    query_point_lights
                );
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Indirect")) {
                draw_indirect_lighting_controls(
                    query_environment_lights,
                    *vxgi_volumes
                );
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

int main(int argc, char** argv) {
    const auto args = parse_scene_arguments(argc, argv);
    if (args.show_help) {
        print_help();
        return 0;
    }

    info("sample-scene using {} graphics backend", backend_name(args.backend));

    App app;
    app.add_plugin<AssetsPlugin>().add_plugin<ImagePlugin>();
    add_graphics_backend(app, args.backend);
    app.add_plugin<RenderingPlugin>()
        .add_plugin<PbrPlugin>()
        .add_plugin<InputPlugin>()
        .add_plugin<TimePlugin>()
        .add_plugin<EnvironmentMapPlugin>()
        .add_plugin<ScenePlugin>()
        .add_plugin<ReflectionPlugin>()
        .add_plugin<LuaScriptingPlugin>()
        .add_systems(PreStartUp, configure_vxgi)
        .add_systems(PreStartUp, setup)
        .add_systems(Update, update_directional_light);

    app.add_plugin<ImGuiPlugin>().add_systems(
        RenderUpdate,
        update_imgui | in_set<RenderingSystems::Render>() | main_thread()
    );

    app.add_plugin(
        devtools::CorePlugin {devtools::Config {
            .host = "127.0.0.1",
            .port = 8080,
        }}
    );
    app.add_plugin(devtools::ecs::ProviderPlugin {});
    app.add_plugin(devtools::rendering::ProviderPlugin {});
    app.add_plugin(
        devtools::pbr::ProviderPlugin {devtools::pbr::Config {
            .jpeg_quality = 80,
        }}
    );
    app.add_plugin(devtools::profiling::ProviderPlugin {});
    app.add_plugin(devtools::reflection::ProviderPlugin {});
    app.add_plugin(devtools::input::ProviderPlugin {});

    app.run();

    return 0;
}
