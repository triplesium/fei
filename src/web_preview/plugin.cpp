#include "web_preview/plugin.hpp"

#include "app/app.hpp"
#include "ecs/system_params.hpp"
#include "graphics/texture.hpp"
#include "pbr/passes/deferred.hpp"
#include "pbr/passes/target.hpp"
#include "web_preview/frame_cache.hpp"
#include "web_preview/frame_capture.hpp"
#include "web_preview/server.hpp"

#include <memory>
#include <string>
#include <utility>

namespace fei {

namespace {

struct SelectedWebPreviewTarget {
    std::shared_ptr<Texture> texture;
    std::string name;
};

SelectedWebPreviewTarget select_web_preview_target(World& world) {
    if (world.has_resource<DeferedRenderResources>()) {
        auto& resources = world.resource<DeferedRenderResources>();
        if (resources.composite_lighting) {
            return {
                .texture = resources.composite_lighting,
                .name = "deferred.composite_lighting",
            };
        }
    }

    if (world.has_resource<RenderTarget>()) {
        auto& target = world.resource<RenderTarget>();
        if (target.color_texture) {
            return {
                .texture = target.color_texture,
                .name = "render_target.color_texture",
            };
        }
    }

    return {};
}

void capture_web_preview_frame(WorldRef world, Res<WebPreviewServer> server) {
    auto cache = server->frame_cache();
    cache->mark_frame_tick();

    if (!server->can_accept_frame()) {
        return;
    }

    auto target = select_web_preview_target(*world);
    if (!target.texture) {
        cache->report_failure("No render target texture is available");
        return;
    }

    auto frame = capture_web_preview_texture(target.texture);
    if (frame.rgba.empty()) {
        cache->report_failure(frame.error);
        return;
    }

    server->submit_frame(
        WebPreviewEncodeJob {
            .rgba = std::move(frame.rgba),
            .width = frame.width,
            .height = frame.height,
            .jpeg_quality = server->config().jpeg_quality,
            .target = std::move(target.name),
        }
    );
}

} // namespace

WebPreviewPlugin::WebPreviewPlugin(WebPreviewConfig config) :
    m_config(std::move(config)) {}

void WebPreviewPlugin::setup(App& app) {
    auto cache = std::make_shared<WebPreviewFrameCache>();
    app.add_resource(WebPreviewServer(m_config, std::move(cache)));
    app.resource<WebPreviewServer>().start();
    app.add_systems(RenderLast, capture_web_preview_frame);
}

void WebPreviewPlugin::finish(App&) {}

} // namespace fei
