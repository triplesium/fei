#include "web_preview/plugin.hpp"

#include "app/app.hpp"
#include "base/log.hpp"
#include "ecs/system_config.hpp"
#include "ecs/system_params.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/texture.hpp"
#include "graphics/texture_readback.hpp"
#include "pbr/passes/target.hpp"
#include "rendering/render_graph.hpp"
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
    std::chrono::steady_clock::time_point next_capture_at;
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

SelectedWebPreviewTarget
select_web_preview_target(const Optional<ResRO<RenderTarget>>& render_target) {
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

WebPreviewDebugStats collect_debug_stats(
    const Optional<ResRO<RenderGraph>>& render_graph,
    const GraphicsDevice& graphics_device
) {
    WebPreviewDebugStats debug_stats;
    if (render_graph) {
        const auto& graph_debug = (*render_graph)->debug_info();
        const auto& graph_stats = graph_debug.stats;
        debug_stats.render_graph = WebPreviewRenderGraphStats {
            .compiled = graph_debug.compiled,
            .compile_error = graph_debug.compile_error,
            .total_passes = graph_stats.total_passes,
            .active_passes = graph_stats.active_passes,
            .culled_passes = graph_stats.culled_passes,
            .transient_texture_requests =
                graph_stats.transient_texture_requests,
            .transient_texture_hits = graph_stats.transient_texture_hits,
            .transient_texture_creates = graph_stats.transient_texture_creates,
            .texture_pool_size = graph_stats.texture_pool_size,
            .active_order = graph_debug.active_pass_names,
        };

        debug_stats.render_graph_passes.reserve(graph_debug.passes.size());
        for (const auto& pass : graph_debug.passes) {
            WebPreviewRenderGraphPass pass_stats {
                .index = pass.index,
                .name = pass.name,
                .active = pass.active,
                .side_effect = pass.side_effect,
                .dependencies = pass.dependencies,
            };
            pass_stats.reads.reserve(pass.reads.size());
            for (const auto& read : pass.reads) {
                pass_stats.reads.push_back(
                    WebPreviewRenderGraphTextureUse {
                        .index = read.handle.index,
                        .generation = read.handle.generation,
                        .name = read.texture_name,
                        .access = read.access_name,
                    }
                );
            }
            pass_stats.writes.reserve(pass.writes.size());
            for (const auto& write : pass.writes) {
                pass_stats.writes.push_back(
                    WebPreviewRenderGraphTextureUse {
                        .index = write.handle.index,
                        .generation = write.handle.generation,
                        .name = write.texture_name,
                        .access = write.access_name,
                    }
                );
            }
            debug_stats.render_graph_passes.push_back(std::move(pass_stats));
        }

        debug_stats.render_graph_textures.reserve(graph_debug.textures.size());
        for (const auto& texture : graph_debug.textures) {
            debug_stats.render_graph_textures.push_back(
                WebPreviewRenderGraphTexture {
                    .index = texture.index,
                    .name = texture.name,
                    .active = texture.active,
                    .imported = texture.imported,
                    .width = texture.width,
                    .height = texture.height,
                    .depth = texture.depth,
                    .mip_level = texture.mip_level,
                    .layer = texture.layer,
                    .format = texture.format,
                    .usage = texture.usage,
                    .type = texture.type,
                    .version_count = texture.version_count,
                    .first_active_use = texture.first_active_use,
                    .last_active_use = texture.last_active_use,
                }
            );
        }

        debug_stats.render_graph_resource_sets.reserve(
            graph_debug.resource_sets.size()
        );
        for (const auto& resource_set : graph_debug.resource_sets) {
            WebPreviewRenderGraphResourceSet resource_set_stats {
                .index = resource_set.index,
                .generation = resource_set.generation,
                .pass_index = resource_set.pass_index,
                .name = resource_set.name,
                .active = resource_set.active,
                .resolved = resource_set.resolved,
                .has_layout = resource_set.has_layout,
            };
            resource_set_stats.bindings.reserve(resource_set.bindings.size());
            for (const auto& binding : resource_set.bindings) {
                resource_set_stats.bindings.push_back(
                    WebPreviewRenderGraphResourceSetBinding {
                        .index = binding.index,
                        .kind = binding.kind,
                        .resource_name = binding.resource_name,
                        .valid = binding.valid,
                        .texture_index = binding.texture.index,
                        .texture_generation = binding.texture.generation,
                    }
                );
            }
            debug_stats.render_graph_resource_sets.push_back(
                std::move(resource_set_stats)
            );
        }
    }

    const auto cache_stats = graphics_device.resource_cache_stats();
    debug_stats.graphics_cache = WebPreviewGraphicsCacheStats {
        .framebuffer_requests = cache_stats.framebuffer_requests,
        .framebuffer_hits = cache_stats.framebuffer_hits,
        .framebuffer_creates = cache_stats.framebuffer_creates,
        .resource_set_requests = cache_stats.resource_set_requests,
        .resource_set_hits = cache_stats.resource_set_hits,
        .resource_set_creates = cache_stats.resource_set_creates,
        .framebuffer_cache_size = cache_stats.framebuffer_cache_size,
        .resource_set_cache_size = cache_stats.resource_set_cache_size,
    };
    debug_stats.graphics_cache.resource_set_sources.reserve(
        cache_stats.resource_set_sources.size()
    );
    for (const auto& source : cache_stats.resource_set_sources) {
        debug_stats.graphics_cache.resource_set_sources.push_back(
            WebPreviewResourceSetSourceStats {
                .name = source.name,
                .requests = source.requests,
                .hits = source.hits,
                .creates = source.creates,
                .cache_size = source.cache_size,
            }
        );
    }

    return debug_stats;
}

void capture_web_preview_frame(
    Optional<ResRO<RenderGraph>> render_graph,
    ResRO<GraphicsDevice> graphics_device,
    Optional<ResRO<RenderTarget>> render_target,
    ResRW<WebPreviewServer> server,
    ResRW<WebPreviewReadbackState> readback_state
) {
    auto cache = server->frame_cache();
    cache->mark_frame_tick();
    cache->update_debug_stats(
        collect_debug_stats(render_graph, *graphics_device)
    );

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

    auto target = select_web_preview_target(render_target);
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
    ResRO<WebPreviewServer> server,
    ResRW<KeyInput> input
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
