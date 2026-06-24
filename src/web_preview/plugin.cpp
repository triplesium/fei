#include "web_preview/plugin.hpp"

#include "app/app.hpp"
#include "base/log.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/texture.hpp"
#include "graphics/texture_readback.hpp"
#include "pbr/passes/deferred.hpp"
#include "pbr/passes/target.hpp"
#include "web_preview/server.hpp"
#include "window/input.hpp"

#include <chrono>
#include <string>
#include <unordered_map>
#include <utility>

namespace fei {

namespace {

struct SelectedWebPreviewTarget {
    std::shared_ptr<Texture> texture;
    std::string name;
};

struct WebPreviewReadbackState {
    std::shared_ptr<TextureReadback> readback;
    std::unordered_map<uint64, std::string> target_names;
    std::chrono::steady_clock::time_point next_capture_at {};
    uint64 next_user_data {1};

    uint64 remember_target(std::string name) {
        auto user_data = next_user_data++;
        target_names[user_data] = std::move(name);
        return user_data;
    }

    std::string take_target(uint64 user_data) {
        auto iter = target_names.find(user_data);
        if (iter == target_names.end()) {
            return {};
        }

        auto name = std::move(iter->second);
        target_names.erase(iter);
        return name;
    }

    void forget_target(uint64 user_data) { target_names.erase(user_data); }
};

bool can_capture_now(
    const WebPreviewConfig& config,
    const WebPreviewReadbackState& readback_state,
    std::chrono::steady_clock::time_point now
) {
    return config.max_capture_fps == 0 || readback_state.next_capture_at <= now;
}

void mark_capture_enqueued(
    const WebPreviewConfig& config,
    WebPreviewReadbackState& readback_state,
    std::chrono::steady_clock::time_point now
) {
    if (config.max_capture_fps == 0) {
        return;
    }

    auto interval = std::chrono::nanoseconds {
        1'000'000'000ULL /
        static_cast<unsigned long long>(config.max_capture_fps)
    };
    readback_state.next_capture_at = now + interval;
}

SelectedWebPreviewTarget select_web_preview_target(
    const Optional<CRes<DeferedRenderResources>>& deferred_resources,
    const Optional<CRes<RenderTarget>>& render_target
) {
    if (deferred_resources) {
        if ((*deferred_resources)->composite_lighting) {
            return {
                .texture = (*deferred_resources)->composite_lighting,
                .name = "deferred.composite_lighting",
            };
        }
    }

    if (render_target) {
        if ((*render_target)->color_texture) {
            return {
                .texture = (*render_target)->color_texture,
                .name = "render_target.color_texture",
            };
        }
    }

    return {};
}

void capture_web_preview_frame(
    Optional<CRes<DeferedRenderResources>> deferred_resources,
    Optional<CRes<RenderTarget>> render_target,
    Res<WebPreviewServer> server,
    Res<WebPreviewReadbackState> readback_state
) {
    auto cache = server->frame_cache();
    cache->mark_frame_tick();

    if (server->can_accept_frame()) {
        auto completed = readback_state->readback->poll();
        if (completed) {
            auto target_name =
                readback_state->take_target(completed->user_data);
            server->submit_frame(
                WebPreviewEncodeJob {
                    .rgba = std::move(completed->data),
                    .width = completed->width,
                    .height = completed->height,
                    .jpeg_quality = server->config().jpeg_quality,
                    .target = std::move(target_name),
                }
            );
        }
    }

    auto now = std::chrono::steady_clock::now();
    if (!server->can_accept_frame() ||
        !can_capture_now(server->config(), *readback_state, now) ||
        !readback_state->readback->can_enqueue()) {
        return;
    }

    auto target = select_web_preview_target(deferred_resources, render_target);
    if (!target.texture) {
        cache->report_failure("No render target texture is available");
        return;
    }

    if (target.texture->type() != TextureType::Texture2D ||
        target.texture->format() != PixelFormat::Rgba8Unorm ||
        target.texture->depth() != 1) {
        cache->report_failure(
            "Web preview readback currently requires a 2D Rgba8Unorm texture"
        );
        return;
    }

    auto user_data = readback_state->remember_target(std::move(target.name));
    if (!readback_state->readback->enqueue(
            TextureReadbackRequest {
                .texture = std::move(target.texture),
                .output_format = PixelFormat::Rgba8Unorm,
                .user_data = user_data,
            }
        )) {
        readback_state->forget_target(user_data);
        cache->report_failure("Texture readback queue is full or unsupported");
        return;
    }

    mark_capture_enqueued(server->config(), *readback_state, now);
}

void apply_web_preview_keyboard_input(
    Res<WebPreviewServer> server,
    Res<KeyInput> input
) {
    auto keys = server->input()->pressed_keys();
    for (auto key : keys) {
        input->press(key);
    }
}

} // namespace

WebPreviewPlugin::WebPreviewPlugin(WebPreviewConfig config) :
    m_config(std::move(config)) {}

void WebPreviewPlugin::setup(App& app) {
    if (m_config.handle_input && !app.has_resource<KeyInput>()) {
        fatal(
            "WebPreviewPlugin input handling requires InputPlugin. Add "
            "InputPlugin before WebPreviewPlugin or set handle_input=false."
        );
    }

    app.add_resource(WebPreviewServer(m_config));
    app.add_resource(
        WebPreviewReadbackState {
            .readback =
                app.resource<GraphicsDevice>().create_texture_readback(),
        }
    );
    app.resource<WebPreviewServer>().start();
    if (m_config.handle_input) {
        app.add_systems(
            PreUpdate,
            apply_web_preview_keyboard_input | after(key_input_system)
        );
    }
    app.add_systems(RenderEnd, capture_web_preview_frame);
}

void WebPreviewPlugin::finish(App&) {}

} // namespace fei
