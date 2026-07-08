#include "devtools_rendering/plugin.hpp"

#include "app/app.hpp"
#include "base/log.hpp"
#include "devtools/bridge.hpp"
#include "devtools/capability.hpp"
#include "ecs/commands.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "ecs/world.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/texture.hpp"
#include "graphics/texture_readback.hpp"
#include "pbr/passes/target.hpp"
#include "rendering/render_graph.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

namespace fei::devtools::rendering {

namespace {

using Json = nlohmann::json;

struct SelectedFrameTarget {
    std::shared_ptr<Texture> texture;
    std::string name;
};

struct FrameCaptureState {
    std::shared_ptr<TextureReadback> readback;
    std::unordered_map<uint64, std::string> target_names;
    std::chrono::steady_clock::time_point next_capture_at;
    uint64 next_user_data {1};
    uint64 frame_version {0};

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
    const Config& config,
    const FrameCaptureState& state,
    std::chrono::steady_clock::time_point now
) {
    return config.max_capture_fps == 0 || state.next_capture_at <= now;
}

void mark_capture_enqueued(
    const Config& config,
    FrameCaptureState& state,
    std::chrono::steady_clock::time_point now
) {
    if (config.max_capture_fps == 0) {
        return;
    }

    auto interval = std::chrono::nanoseconds {
        1'000'000'000ULL /
        static_cast<unsigned long long>(config.max_capture_fps)
    };
    state.next_capture_at = now + interval;
}

SelectedFrameTarget
select_frame_target(const Optional<ResRO<RenderTarget>>& render_target) {
    if (render_target && (*render_target)->color_texture) {
        return {
            .texture = (*render_target)->color_texture,
            .name = "render_target.color_texture",
        };
    }
    return {};
}

void append_jpeg_bytes(void* context, void* data, int size) {
    auto& bytes = *static_cast<std::vector<byte>*>(context);
    auto* first = static_cast<byte*>(data);
    bytes.insert(bytes.end(), first, first + size);
}

std::vector<unsigned char> rgba_to_flipped_rgb(
    const std::vector<byte>& rgba,
    uint32 width,
    uint32 height
) {
    std::vector<unsigned char> rgb(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3
    );
    auto row_rgba_size = static_cast<std::size_t>(width) * 4;
    auto row_rgb_size = static_cast<std::size_t>(width) * 3;

    for (uint32 dst_y = 0; dst_y < height; ++dst_y) {
        auto src_y = height - dst_y - 1;
        auto* src = reinterpret_cast<const unsigned char*>(rgba.data()) +
                    static_cast<std::size_t>(src_y) * row_rgba_size;
        auto* dst = rgb.data() + static_cast<std::size_t>(dst_y) * row_rgb_size;
        for (uint32 x = 0; x < width; ++x) {
            dst[x * 3 + 0] = src[x * 4 + 0];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 2];
        }
    }
    return rgb;
}

std::vector<byte> encode_jpeg(
    const std::vector<byte>& rgba,
    uint32 width,
    uint32 height,
    int quality
) {
    if (rgba.empty()) {
        return {};
    }

    auto rgb = rgba_to_flipped_rgb(rgba, width, height);
    std::vector<byte> jpeg;
    auto ok = stbi_write_jpg_to_func(
        append_jpeg_bytes,
        &jpeg,
        static_cast<int>(width),
        static_cast<int>(height),
        3,
        rgb.data(),
        std::clamp(quality, 1, 100)
    );
    if (ok == 0) {
        return {};
    }
    return jpeg;
}

Json texture_use_to_json(const RgTextureUseDebugInfo& use) {
    return Json {
        {"index", use.handle.index},
        {"generation", use.handle.generation},
        {"name", use.texture_name},
        {"access", use.access_name},
    };
}

Json render_graph_json(const Optional<ResRO<RenderGraph>>& render_graph) {
    if (!render_graph) {
        return Json {{"available", false}};
    }

    const auto& debug = (*render_graph)->debug_info();
    const auto& stats = debug.stats;

    Json passes = Json::array();
    for (const auto& pass : debug.passes) {
        Json reads = Json::array();
        for (const auto& read : pass.reads) {
            reads.push_back(texture_use_to_json(read));
        }
        Json writes = Json::array();
        for (const auto& write : pass.writes) {
            writes.push_back(texture_use_to_json(write));
        }
        passes.push_back(
            Json {
                {"index", pass.index},
                {"name", pass.name},
                {"active", pass.active},
                {"side_effect", pass.side_effect},
                {"dependencies", pass.dependencies},
                {"reads", std::move(reads)},
                {"writes", std::move(writes)},
            }
        );
    }

    Json textures = Json::array();
    for (const auto& texture : debug.textures) {
        textures.push_back(
            Json {
                {"index", texture.index},
                {"name", texture.name},
                {"active", texture.active},
                {"imported", texture.imported},
                {"width", texture.width},
                {"height", texture.height},
                {"depth", texture.depth},
                {"mip_level", texture.mip_level},
                {"layer", texture.layer},
                {"format", texture.format},
                {"usage", texture.usage},
                {"type", texture.type},
                {"sample_count", texture.sample_count},
                {"version_count", texture.version_count},
                {"first_active_use", texture.first_active_use},
                {"last_active_use", texture.last_active_use},
            }
        );
    }

    Json resource_sets = Json::array();
    for (const auto& resource_set : debug.resource_sets) {
        Json bindings = Json::array();
        for (const auto& binding : resource_set.bindings) {
            Json binding_json {
                {"index", binding.index},
                {"kind", binding.kind},
                {"resource_name", binding.resource_name},
                {"valid", binding.valid},
            };
            if (binding.kind == "texture") {
                binding_json["texture_index"] = binding.texture.index;
                binding_json["texture_generation"] = binding.texture.generation;
            }
            bindings.push_back(std::move(binding_json));
        }
        resource_sets.push_back(
            Json {
                {"index", resource_set.index},
                {"generation", resource_set.generation},
                {"pass_index", resource_set.pass_index},
                {"name", resource_set.name},
                {"active", resource_set.active},
                {"resolved", resource_set.resolved},
                {"has_layout", resource_set.has_layout},
                {"bindings", std::move(bindings)},
            }
        );
    }

    return Json {
        {"available", true},
        {"compiled", debug.compiled},
        {"compile_error", debug.compile_error},
        {"total_passes", stats.total_passes},
        {"active_passes", stats.active_passes},
        {"culled_passes", stats.culled_passes},
        {"transient_texture_requests", stats.transient_texture_requests},
        {"transient_texture_hits", stats.transient_texture_hits},
        {"transient_texture_creates", stats.transient_texture_creates},
        {"texture_pool_size", stats.texture_pool_size},
        {"active_order", debug.active_pass_names},
        {"passes", std::move(passes)},
        {"textures", std::move(textures)},
        {"resource_sets", std::move(resource_sets)},
    };
}

Json graphics_cache_json(const GraphicsDevice& device) {
    auto stats = device.resource_cache_stats();
    Json sources = Json::array();
    for (const auto& source : stats.resource_set_sources) {
        sources.push_back(
            Json {
                {"name", source.name},
                {"requests", source.requests},
                {"hits", source.hits},
                {"creates", source.creates},
                {"cache_size", source.cache_size},
            }
        );
    }
    return Json {
        {"framebuffer_requests", stats.framebuffer_requests},
        {"framebuffer_hits", stats.framebuffer_hits},
        {"framebuffer_creates", stats.framebuffer_creates},
        {"framebuffer_cache_size", stats.framebuffer_cache_size},
        {"resource_set_requests", stats.resource_set_requests},
        {"resource_set_hits", stats.resource_set_hits},
        {"resource_set_creates", stats.resource_set_creates},
        {"resource_set_cache_size", stats.resource_set_cache_size},
        {"resource_set_sources", std::move(sources)},
    };
}

void respond_snapshot_requests(
    Query<Entity, const Request, const SnapshotRequest> requests,
    Commands commands,
    const std::string& capability,
    const std::string& schema,
    const std::string& json,
    uint64 version
) {
    auto& world = commands.world();
    for (auto [entity, request, snapshot_request] : requests) {
        (void)snapshot_request;
        if (request.capability != capability) {
            continue;
        }
        if (!world.has_entity(entity)) {
            continue;
        }
        commands.spawn().add(
            SnapshotResponse {
                .token = request.token,
                .capability = capability,
                .json = json,
                .schema = schema,
                .version = version,
            }
        );
        commands.entity(entity).despawn();
    }
}

void publish_rendering_snapshots(
    Optional<ResRO<RenderGraph>> render_graph,
    ResRO<GraphicsDevice> graphics_device,
    Query<Entity, const Request, const SnapshotRequest> requests,
    Commands commands
) {
    static uint64 render_graph_version = 0;
    static uint64 graphics_cache_version = 0;
    ++render_graph_version;
    ++graphics_cache_version;

    auto graph = render_graph_json(render_graph).dump();
    auto cache = graphics_cache_json(*graphics_device).dump();

    commands.spawn().add(
        SnapshotResponse {
            .capability = "rendering.render_graph",
            .json = graph,
            .schema = "rendering.render_graph.v1",
            .version = render_graph_version,
        }
    );
    commands.spawn().add(
        SnapshotResponse {
            .capability = "graphics.cache",
            .json = cache,
            .schema = "graphics.cache.v1",
            .version = graphics_cache_version,
        }
    );

    respond_snapshot_requests(
        requests,
        commands,
        "rendering.render_graph",
        "rendering.render_graph.v1",
        graph,
        render_graph_version
    );
    respond_snapshot_requests(
        requests,
        commands,
        "graphics.cache",
        "graphics.cache.v1",
        cache,
        graphics_cache_version
    );
}

bool has_frame_demand(
    Query<Entity, const Request, const BlobRequest> requests,
    Query<const Subscription> subscriptions
) {
    for (auto [entity, request, blob_request] : requests) {
        (void)entity;
        (void)blob_request;
        if (request.capability == "rendering.frame") {
            return true;
        }
    }
    for (auto [subscription] : subscriptions) {
        if (subscription.capability == "rendering.frame") {
            return true;
        }
    }
    return false;
}

void respond_frame_errors(
    Query<Entity, const Request, const BlobRequest> requests,
    Commands commands,
    std::string message
) {
    for (auto [entity, request, blob_request] : requests) {
        (void)blob_request;
        if (request.capability != "rendering.frame") {
            continue;
        }
        commands.spawn().add(
            ErrorResponse {
                .token = request.token,
                .capability = request.capability,
                .status = 503,
                .message = message,
            }
        );
        commands.entity(entity).despawn();
    }
}

void publish_completed_frame(
    Query<Entity, const Request, const BlobRequest> requests,
    Commands commands,
    std::vector<byte> jpeg,
    uint32 width,
    uint32 height,
    std::string target,
    uint64 version
) {
    std::unordered_map<std::string, std::string> metadata {
        {"width", std::to_string(width)},
        {"height", std::to_string(height)},
        {"target", target},
    };

    commands.spawn().add(
        BlobResponse {
            .capability = "rendering.frame",
            .bytes = jpeg,
            .mime = "image/jpeg",
            .version = version,
            .metadata = metadata,
        }
    );

    for (auto [entity, request, blob_request] : requests) {
        (void)blob_request;
        if (request.capability != "rendering.frame") {
            continue;
        }
        commands.spawn().add(
            BlobResponse {
                .token = request.token,
                .capability = request.capability,
                .bytes = jpeg,
                .mime = "image/jpeg",
                .version = version,
                .metadata = metadata,
            }
        );
        commands.entity(entity).despawn();
    }
}

void capture_rendering_frame(
    Optional<ResRO<RenderTarget>> render_target,
    ResRW<FrameCaptureState> state,
    ResRO<Config> config,
    Query<Entity, const Request, const BlobRequest> requests,
    Query<const Subscription> subscriptions,
    Commands commands
) {
    if (auto completed = state->readback->poll()) {
        auto target = state->take_target(completed->user_data);
        auto jpeg = encode_jpeg(
            completed->data,
            completed->width,
            completed->height,
            config->jpeg_quality
        );
        if (jpeg.empty()) {
            respond_frame_errors(requests, commands, "JPEG encoding failed");
        } else {
            ++state->frame_version;
            publish_completed_frame(
                requests,
                commands,
                std::move(jpeg),
                completed->width,
                completed->height,
                std::move(target),
                state->frame_version
            );
        }
    }

    if (!has_frame_demand(requests, subscriptions)) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    if (!can_capture_now(*config, *state, now) ||
        !state->readback->can_enqueue()) {
        return;
    }

    auto target = select_frame_target(render_target);
    if (!target.texture) {
        respond_frame_errors(
            requests,
            commands,
            "No render target texture is available"
        );
        return;
    }
    if (target.texture->type() != TextureType::Texture2D ||
        target.texture->format() != PixelFormat::Rgba8Unorm ||
        target.texture->depth() != 1) {
        respond_frame_errors(
            requests,
            commands,
            "DevTools frame capture requires a 2D Rgba8Unorm texture"
        );
        return;
    }

    auto user_data = state->remember_target(std::move(target.name));
    if (!state->readback->enqueue(
            TextureReadbackRequest {
                .texture = std::move(target.texture),
                .output_format = PixelFormat::Rgba8Unorm,
                .user_data = user_data,
            }
        )) {
        state->forget_target(user_data);
        respond_frame_errors(
            requests,
            commands,
            "Texture readback queue is full or unsupported"
        );
        return;
    }

    mark_capture_enqueued(*config, *state, now);
}

} // namespace

ProviderPlugin::ProviderPlugin(Config config) : m_config(config) {}

void ProviderPlugin::setup(App& app) {
    if (!app.has_resource<Bridge>()) {
        fatal(
            "devtools::rendering::ProviderPlugin requires "
            "devtools::CorePlugin. Add devtools::CorePlugin before "
            "devtools::rendering::ProviderPlugin."
        );
    }

    declare_capability(
        app.world(),
        "rendering.frame",
        "Rendered Frame",
        BlobCapability {
            .mime = "image/jpeg",
            .mode = PublishMode::OnDemand,
            .waitable = true,
        }
    );
    declare_capability(
        app.world(),
        "rendering.render_graph",
        "Render Graph",
        SnapshotCapability {
            .schema = "rendering.render_graph.v1",
            .mode = PublishMode::Cached,
        }
    );
    declare_capability(
        app.world(),
        "graphics.cache",
        "Graphics Cache",
        SnapshotCapability {
            .schema = "graphics.cache.v1",
            .mode = PublishMode::Cached,
        }
    );

    app.add_resource(Config {m_config});
    app.add_resource(
        FrameCaptureState {
            .readback =
                app.resource<GraphicsDevice>().create_texture_readback(),
        }
    );
    app.add_systems(RenderEnd, publish_rendering_snapshots);
    app.add_systems(RenderEnd, capture_rendering_frame);
}

void ProviderPlugin::finish(App&) {}

} // namespace fei::devtools::rendering
