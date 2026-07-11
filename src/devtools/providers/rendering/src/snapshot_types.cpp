#include "snapshot_types.hpp"

#include <utility>

namespace fei::devtools::rendering {
namespace {

TextureUseSnapshot make_texture_use_snapshot(const RgTextureUseDebugInfo& use) {
    return TextureUseSnapshot {
        .index = use.handle.index,
        .generation = use.handle.generation,
        .name = use.texture_name,
        .access = use.access_name,
    };
}

} // namespace

RenderGraphSnapshot make_render_graph_snapshot(const RgDebugInfo& debug) {
    RenderGraphSnapshot snapshot {
        .available = true,
        .compiled = debug.compiled,
        .compile_error = debug.compile_error,
        .total_passes = debug.stats.total_passes,
        .active_passes = debug.stats.active_passes,
        .culled_passes = debug.stats.culled_passes,
        .transient_texture_requests = debug.stats.transient_texture_requests,
        .transient_texture_hits = debug.stats.transient_texture_hits,
        .transient_texture_creates = debug.stats.transient_texture_creates,
        .texture_pool_size = debug.stats.texture_pool_size,
        .active_order = debug.active_pass_names,
    };

    snapshot.passes.reserve(debug.passes.size());
    for (const auto& pass : debug.passes) {
        RenderPassSnapshot pass_snapshot {
            .index = pass.index,
            .name = pass.name,
            .active = pass.active,
            .side_effect = pass.side_effect,
            .dependencies = pass.dependencies,
        };
        pass_snapshot.reads.reserve(pass.reads.size());
        for (const auto& read : pass.reads) {
            pass_snapshot.reads.push_back(make_texture_use_snapshot(read));
        }
        pass_snapshot.writes.reserve(pass.writes.size());
        for (const auto& write : pass.writes) {
            pass_snapshot.writes.push_back(make_texture_use_snapshot(write));
        }
        snapshot.passes.push_back(std::move(pass_snapshot));
    }

    snapshot.textures.reserve(debug.textures.size());
    for (const auto& texture : debug.textures) {
        snapshot.textures.push_back(
            TextureSnapshot {
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
                .sample_count = texture.sample_count,
                .version_count = texture.version_count,
                .first_active_use = texture.first_active_use,
                .last_active_use = texture.last_active_use,
            }
        );
    }

    snapshot.resource_sets.reserve(debug.resource_sets.size());
    for (const auto& resource_set : debug.resource_sets) {
        ResourceSetSnapshot resource_set_snapshot {
            .index = resource_set.index,
            .generation = resource_set.generation,
            .pass_index = resource_set.pass_index,
            .name = resource_set.name,
            .active = resource_set.active,
            .resolved = resource_set.resolved,
            .has_layout = resource_set.has_layout,
        };
        resource_set_snapshot.bindings.reserve(resource_set.bindings.size());
        for (const auto& binding : resource_set.bindings) {
            const auto has_texture = binding.kind == "texture";
            resource_set_snapshot.bindings.push_back(
                ResourceBindingSnapshot {
                    .index = binding.index,
                    .kind = binding.kind,
                    .resource_name = binding.resource_name,
                    .valid = binding.valid,
                    .has_texture = has_texture,
                    .texture_index = has_texture ?
                                         binding.texture.index :
                                         RgTextureHandle::InvalidIndex,
                    .texture_generation =
                        has_texture ? binding.texture.generation : 0,
                }
            );
        }
        snapshot.resource_sets.push_back(std::move(resource_set_snapshot));
    }

    return snapshot;
}

GraphicsCacheSnapshot
make_graphics_cache_snapshot(const GraphicsResourceCacheStats& stats) {
    GraphicsCacheSnapshot snapshot {
        .framebuffer_requests = stats.framebuffer_requests,
        .framebuffer_hits = stats.framebuffer_hits,
        .framebuffer_creates = stats.framebuffer_creates,
        .framebuffer_cache_size = stats.framebuffer_cache_size,
        .resource_set_requests = stats.resource_set_requests,
        .resource_set_hits = stats.resource_set_hits,
        .resource_set_creates = stats.resource_set_creates,
        .resource_set_cache_size = stats.resource_set_cache_size,
    };
    snapshot.resource_set_sources.reserve(stats.resource_set_sources.size());
    for (const auto& source : stats.resource_set_sources) {
        snapshot.resource_set_sources.push_back(
            ResourceSetSourceSnapshot {
                .name = source.name,
                .requests = source.requests,
                .hits = source.hits,
                .creates = source.creates,
                .cache_size = source.cache_size,
            }
        );
    }
    return snapshot;
}

} // namespace fei::devtools::rendering
