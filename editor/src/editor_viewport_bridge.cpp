#include "editor/editor_viewport_bridge.hpp"

#include "app/app.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "ecs/world.hpp"
#include "editor/scene_panel.hpp"
#include "imgui/renderer.hpp"
#include "imgui/texture.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_app.hpp"
#include "sprite/output.hpp"
#include "sprite/plugin.hpp"

namespace fei::editor {
namespace {

struct ExtractedEditorViewport {
    ImGuiTextureHandle texture;
    uint32 width {0};
    uint32 height {0};
};

struct EditorRenderSystems {
    struct BindViewport : SystemSet<BindViewport> {};
};

void extract_editor_viewport(
    Extract<ResRO<SceneViewport>> editor,
    ResRW<ExtractedEditorViewport> viewport,
    ResRW<SpriteOutput> output
) {
    viewport->texture = (*editor)->texture;
    viewport->width = (*editor)->visible ? (*editor)->width : 0;
    viewport->height = (*editor)->visible ? (*editor)->height : 0;
    output->resize(viewport->width, viewport->height);
}

void bind_editor_viewport(
    ResRO<ExtractedEditorViewport> viewport,
    ResRO<SpriteOutput> output,
    ResRW<ImGuiTextureRegistry> textures
) {
    if (!viewport->texture) {
        return;
    }
    if (!output->texture) {
        textures->unbind_render_texture(viewport->texture);
        return;
    }
    textures->bind_render_texture(viewport->texture, output->texture);
}

void shutdown_editor_viewport(World& world) {
    if (!world.has_resource<ExtractedEditorViewport>() ||
        !world.has_resource<ImGuiTextureRegistry>()) {
        return;
    }
    world.resource<ImGuiTextureRegistry>().unbind_render_texture(
        world.resource<ExtractedEditorViewport>().texture
    );
}

} // namespace

void install_editor_viewport_bridge(App& app) {
    const auto scene_texture =
        app.resource<ImGuiRenderTextures>().reserve_texture();
    app.add_resource(SceneViewport {.texture = scene_texture});

    auto& render_app = app.sub_app<RenderApp>();
    render_app.add_resource(ExtractedEditorViewport {})
        .add_shutdown(shutdown_editor_viewport)
        .configure_sets(
            RenderUpdate,
            EditorRenderSystems::BindViewport {}
                .after<SpriteSystems::PrepareOutput>()
        )
        .add_systems(RenderExtract, extract_editor_viewport)
        .add_systems(
            RenderUpdate,
            bind_editor_viewport |
                in_set<RenderingSystems::PrepareResources>() |
                in_set<EditorRenderSystems::BindViewport>()
        );
}

void cleanup_editor_viewport_bridge(App& app) noexcept {
    if (!app.has_resource<SceneViewport>()) {
        return;
    }

    auto& viewport = app.resource<SceneViewport>();
    if (viewport.texture && app.has_resource<ImGuiRenderTextures>()) {
        app.resource<ImGuiRenderTextures>().release_texture(viewport.texture);
        viewport.texture = {};
    }
}

} // namespace fei::editor
