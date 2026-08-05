#include "editor/plugin.hpp"

#include "app/app.hpp"
#include "app/reflection_plugin.hpp"
#include "asset/database.hpp"
#include "asset/importer.hpp"
#include "base/log.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "editor/editor_viewport_bridge.hpp"
#include "editor/project_asset_sync.hpp"
#include "editor/project_session.hpp"
#include "imgui/plugin.hpp"
#include "imgui/texture.hpp"
#include "rendering/plugin.hpp"
#include "rendering/render_app.hpp"
#include "sprite/output.hpp"
#include "sprite/plugin.hpp"

namespace fei::editor {

namespace {

void update_project_assets(
    ResRW<ProjectAssetSynchronizer> synchronizer,
    WorldRef world
) {
    synchronizer->update(*world);
}

} // namespace

void EditorPlugin::setup(App& app) {
    if (!app.has_plugin<ReflectionPlugin>()) {
        fatal("EditorPlugin requires ReflectionPlugin to be installed first");
    }
    if (!app.has_plugin<RenderingPlugin>()) {
        fatal("EditorPlugin requires RenderingPlugin to be installed first");
    }
    if (!app.has_plugin<SpritePlugin>()) {
        fatal("EditorPlugin requires SpritePlugin to be installed first");
    }
    if (!app.has_plugin<ImGuiPlugin>()) {
        fatal("EditorPlugin requires ImGuiPlugin to be installed first");
    }
    if (!app.has_resource<AssetDatabase>() ||
        !app.has_resource<AssetImporterRegistry>()) {
        fatal("EditorPlugin requires AssetsPlugin to be installed first");
    }
    auto& render_app = app.sub_app<RenderApp>();
    if (!render_app.has_resource<SpriteOutput>() ||
        static_cast<const SubApp&>(render_app).resource<SpriteOutput>().mode !=
            SpriteOutputMode::Texture) {
        fatal("EditorPlugin requires SpritePlugin texture output mode");
    }
    if (!app.has_resource<ImGuiImages>() ||
        !app.has_resource<ImGuiRenderTextures>()) {
        fatal("EditorPlugin requires ImGui texture services");
    }

    install_editor_viewport_bridge(app);

    EditorProjectSession project_session;
    if (auto status = project_session.open(app, m_config.create_welcome_scene);
        !status) {
        fatal("Failed to open editor project: {}", status.error());
    }
    app.add_resource(project_session);

    app.add_systems(Update, update_project_assets | main_thread());
}

void EditorPlugin::cleanup(App& app) noexcept {
    cleanup_editor_viewport_bridge(app);
}

} // namespace fei::editor
