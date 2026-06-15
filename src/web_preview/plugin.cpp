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
#include <utility>

namespace fei {

namespace {

std::shared_ptr<Texture> select_web_preview_target(World& world) {
    if (world.has_resource<DeferedRenderResources>()) {
        auto& resources = world.resource<DeferedRenderResources>();
        if (resources.composite_lighting) {
            return resources.composite_lighting;
        }
    }

    if (world.has_resource<RenderTarget>()) {
        auto& target = world.resource<RenderTarget>();
        if (target.color_texture) {
            return target.color_texture;
        }
    }

    return {};
}

void capture_web_preview_frame(WorldRef world, Res<WebPreviewServer> server) {
    auto texture = select_web_preview_target(*world);
    if (!texture) {
        return;
    }

    auto frame =
        capture_web_preview_texture(texture, server->config().jpeg_quality);
    if (frame.jpeg.empty()) {
        return;
    }

    server->frame_cache()
        ->publish_jpeg(std::move(frame.jpeg), frame.width, frame.height);
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
