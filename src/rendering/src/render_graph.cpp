#include "rendering/render_graph.hpp"

#include <algorithm>
#include <vector>

namespace fei {

namespace {

void hash_combine(std::size_t& seed, std::size_t value) {
    seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

} // namespace

std::size_t RenderGraph::ResourceSetCacheKeyHash::operator()(
    const RenderGraph::ResourceSetCacheKey& key
) const {
    std::size_t seed = std::hash<const ResourceLayout*> {}(key.layout);
    for (const auto& resource : key.resources) {
        hash_combine(
            seed,
            std::hash<const BindableResource*> {}(resource.resource)
        );
        hash_combine(seed, resource.offset);
        hash_combine(seed, resource.size);
    }
    hash_combine(seed, key.resources.size());
    return seed;
}

RgTextureHandle
RenderGraphBuilder::create_texture(std::string name, TextureDescription desc) {
    return m_graph.create_texture(std::move(name), desc);
}

RgTextureHandle RenderGraphBuilder::import_texture(
    std::string name,
    std::shared_ptr<Texture> texture
) {
    return m_graph.import_texture(std::move(name), std::move(texture));
}

void RenderGraphBuilder::read_texture(
    RgTextureHandle handle,
    RenderGraphAccess access
) {
    m_graph.read_texture(m_pass_index, handle, access);
}

RgTextureHandle RenderGraphBuilder::write_texture(
    RgTextureHandle handle,
    RenderGraphAccess access
) {
    return m_graph.write_texture(m_pass_index, handle, access);
}

RgResourceSetHandle RenderGraphBuilder::create_resource_set(
    std::string name,
    std::shared_ptr<const ResourceLayout> layout,
    std::vector<RenderGraphResourceBinding> bindings
) {
    return m_graph.create_resource_set(
        m_pass_index,
        std::move(name),
        std::move(layout),
        std::move(bindings)
    );
}

void RenderGraphBuilder::side_effect() {
    m_graph.side_effect(m_pass_index);
}

std::shared_ptr<Texture>
RenderGraphContext::texture(RgTextureHandle handle) const {
    return m_graph.texture_resource(handle).texture;
}

std::shared_ptr<ResourceSet>
RenderGraphContext::resource_set(RgResourceSetHandle handle) const {
    return m_graph.resolve_resource_set(handle, m_device);
}

void RenderGraph::clear() {
    ++m_frame_index;
    for (auto& texture : m_textures) {
        release_texture(texture);
    }
    collect_texture_pool();
    collect_resource_set_cache();

    m_textures.clear();
    m_resource_sets.clear();
    m_passes.clear();
    m_compiled_order.clear();
    m_blackboard.clear();
    m_compile_error.clear();
    m_compiled = false;
    m_stats.total_passes = 0;
    m_stats.active_passes = 0;
    m_stats.culled_passes = 0;
    m_stats.transient_texture_requests = 0;
    m_stats.transient_texture_hits = 0;
    m_stats.transient_texture_creates = 0;
    update_texture_pool_stats();
    m_debug_info = {};
    sync_debug_stats();
}

RenderGraph::TexturePoolKey
RenderGraph::texture_pool_key(const TextureDescription& desc) {
    return TexturePoolKey {
        .width = desc.width,
        .height = desc.height,
        .depth = desc.depth,
        .mip_level = desc.mip_level,
        .layer = desc.layer,
        .texture_format = desc.texture_format,
        .texture_usage = desc.texture_usage.to_raw(),
        .texture_type = desc.texture_type,
        .sample_count = desc.sample_count,
    };
}

bool RenderGraph::is_valid(RgTextureHandle handle) const {
    return handle.is_valid() && handle.index < m_textures.size() &&
           handle.generation < m_textures[handle.index].versions.size();
}

bool RenderGraph::is_valid(RgResourceSetHandle handle) const {
    return handle.is_valid() && handle.index < m_resource_sets.size() &&
           handle.generation == m_resource_sets[handle.index].generation;
}

RenderGraph::TextureResource&
RenderGraph::texture_resource(RgTextureHandle handle) {
    return m_textures.at(handle.index);
}

const RenderGraph::TextureResource&
RenderGraph::texture_resource(RgTextureHandle handle) const {
    return m_textures.at(handle.index);
}

RenderGraph::ResourceSetResource&
RenderGraph::resource_set_resource(RgResourceSetHandle handle) {
    return m_resource_sets.at(handle.index);
}

const RenderGraph::ResourceSetResource&
RenderGraph::resource_set_resource(RgResourceSetHandle handle) const {
    return m_resource_sets.at(handle.index);
}

RgTextureHandle
RenderGraph::create_texture(std::string name, TextureDescription desc) {
    RgTextureHandle handle {
        .index = static_cast<uint32>(m_textures.size()),
        .generation = 0,
    };
    m_textures.push_back(
        TextureResource {
            .name = std::move(name),
            .description = desc,
            .texture = nullptr,
            .imported = false,
            .versions = {TextureVersion {}},
        }
    );
    m_compiled = false;
    return handle;
}

RgTextureHandle RenderGraph::import_texture(
    std::string name,
    std::shared_ptr<Texture> texture
) {
    TextureDescription desc {};
    if (texture) {
        desc = TextureDescription {
            .width = texture->width(),
            .height = texture->height(),
            .depth = texture->depth(),
            .mip_level = texture->mip_level(),
            .layer = texture->layer(),
            .texture_format = texture->format(),
            .texture_usage = texture->usage(),
            .texture_type = texture->type(),
            .sample_count = texture->sample_count(),
        };
    }

    RgTextureHandle handle {
        .index = static_cast<uint32>(m_textures.size()),
        .generation = 0,
    };
    m_textures.push_back(
        TextureResource {
            .name = std::move(name),
            .description = desc,
            .texture = std::move(texture),
            .imported = true,
            .versions = {TextureVersion {}},
        }
    );
    m_compiled = false;
    return handle;
}

void RenderGraph::read_texture(
    uint32 pass_index,
    RgTextureHandle handle,
    RenderGraphAccess access
) {
    m_passes.at(pass_index)
        ->reads.push_back(TextureUse {.handle = handle, .access = access});
    m_compiled = false;
}

RgTextureHandle RenderGraph::write_texture(
    uint32 pass_index,
    RgTextureHandle handle,
    RenderGraphAccess access
) {
    if (!is_valid(handle)) {
        return {};
    }

    auto& resource = texture_resource(handle);
    auto next_generation = static_cast<uint32>(resource.versions.size());
    RgTextureHandle new_handle {
        .index = handle.index,
        .generation = next_generation,
    };
    resource.versions.push_back(
        TextureVersion {
            .writer_pass = static_cast<int>(pass_index),
            .write_access = access,
        }
    );
    m_passes.at(pass_index)
        ->writes.push_back(TextureUse {.handle = new_handle, .access = access});
    m_compiled = false;
    return new_handle;
}

RgResourceSetHandle RenderGraph::create_resource_set(
    uint32 pass_index,
    std::string name,
    std::shared_ptr<const ResourceLayout> layout,
    std::vector<RenderGraphResourceBinding> bindings
) {
    RgResourceSetHandle handle {
        .index = static_cast<uint32>(m_resource_sets.size()),
        .generation = 0,
    };

    for (const auto& binding : bindings) {
        if (binding.is_texture()) {
            const auto texture = binding.texture();
            const auto& pass_reads = m_passes.at(pass_index)->reads;
            const auto already_read =
                std::ranges::find_if(pass_reads, [&](const auto& read) {
                    return read.handle == texture &&
                           read.access == RenderGraphAccess::TextureRead;
                }) != pass_reads.end();
            if (!already_read) {
                read_texture(
                    pass_index,
                    texture,
                    RenderGraphAccess::TextureRead
                );
            }
        }
    }

    m_resource_sets.push_back(
        ResourceSetResource {
            .name = std::move(name),
            .layout = std::move(layout),
            .bindings = std::move(bindings),
            .resource_set = nullptr,
            .pass_index = pass_index,
            .generation = handle.generation,
        }
    );
    m_compiled = false;
    return handle;
}

void RenderGraph::side_effect(uint32 pass_index) {
    m_passes.at(pass_index)->side_effect = true;
    m_compiled = false;
}

std::shared_ptr<Texture> RenderGraph::acquire_texture(
    const GraphicsDevice& device,
    const TextureDescription& desc
) {
    ++m_stats.transient_texture_requests;
    const auto key = texture_pool_key(desc);
    auto pool = m_texture_pool.find(key);
    if (pool != m_texture_pool.end() && !pool->second.empty()) {
        ++m_stats.transient_texture_hits;
        auto texture = std::move(pool->second.back().texture);
        pool->second.pop_back();
        if (pool->second.empty()) {
            m_texture_pool.erase(pool);
        }
        update_texture_pool_stats();
        return texture;
    }
    ++m_stats.transient_texture_creates;
    return device.create_texture(desc);
}

void RenderGraph::release_texture(TextureResource& resource) {
    if (resource.imported || !resource.texture) {
        return;
    }

    auto key = texture_pool_key(resource.description);
    m_texture_pool[key].push_back(
        PooledTexture {
            .texture = std::move(resource.texture),
            .last_used_frame = m_frame_index,
        }
    );
    resource.texture.reset();
    update_texture_pool_stats();
}

std::size_t RenderGraph::texture_pool_size() const {
    std::size_t size = 0;
    for (const auto& [_, textures] : m_texture_pool) {
        size += textures.size();
    }
    return size;
}

void RenderGraph::update_texture_pool_stats() {
    m_stats.texture_pool_size = texture_pool_size();
}

void RenderGraph::collect_texture_pool() {
    for (auto pool = m_texture_pool.begin(); pool != m_texture_pool.end();) {
        auto& textures = pool->second;
        textures.erase(
            std::remove_if(
                textures.begin(),
                textures.end(),
                [&](const PooledTexture& texture) {
                    if (!texture.texture) {
                        return true;
                    }
                    const auto age =
                        m_frame_index >= texture.last_used_frame ?
                            m_frame_index - texture.last_used_frame :
                            0;
                    return age > TexturePoolMaxIdleFrames;
                }
            ),
            textures.end()
        );

        if (textures.empty()) {
            pool = m_texture_pool.erase(pool);
        } else {
            ++pool;
        }
    }
    update_texture_pool_stats();
}

void RenderGraph::collect_resource_set_cache() {
    for (auto cached = m_resource_set_cache.begin();
         cached != m_resource_set_cache.end();) {
        if (!cached->second.resource_set) {
            cached = m_resource_set_cache.erase(cached);
            continue;
        }

        const auto age = m_frame_index >= cached->second.last_used_frame ?
                             m_frame_index - cached->second.last_used_frame :
                             0;
        if (age > TexturePoolMaxIdleFrames) {
            cached = m_resource_set_cache.erase(cached);
        } else {
            ++cached;
        }
    }
}

void RenderGraph::ensure_texture_allocated(
    RgTextureHandle handle,
    const GraphicsDevice& device
) {
    auto& resource = texture_resource(handle);
    if (resource.imported || resource.texture) {
        return;
    }
    resource.texture = acquire_texture(device, resource.description);
}

std::shared_ptr<ResourceSet> RenderGraph::resolve_resource_set(
    RgResourceSetHandle handle,
    const GraphicsDevice& device
) {
    if (!is_valid(handle)) {
        return nullptr;
    }

    auto& resource_set = resource_set_resource(handle);
    std::vector<std::shared_ptr<const BindableResource>> resources;
    resources.reserve(resource_set.bindings.size());
    ResourceSetCacheKey key {
        .layout = resource_set.layout.get(),
    };
    key.resources.reserve(resource_set.bindings.size());

    for (const auto& binding : resource_set.bindings) {
        std::shared_ptr<const BindableResource> resource;
        if (binding.is_texture()) {
            resource = texture_resource(binding.texture()).texture;
        } else {
            resource = binding.resource();
        }
        resources.push_back(resource);
        if (auto range =
                std::dynamic_pointer_cast<const BufferRange>(resource)) {
            key.resources.push_back(
                BindableResourceCacheKey {
                    .resource = range->buffer().get(),
                    .offset = range->offset(),
                    .size = range->size(),
                }
            );
        } else {
            key.resources.push_back(
                BindableResourceCacheKey {
                    .resource = resource.get(),
                    .offset = 0,
                    .size = BufferRange::WholeSize,
                }
            );
        }
    }

    if (auto cached = m_resource_set_cache.find(key);
        cached != m_resource_set_cache.end()) {
        cached->second.last_used_frame = m_frame_index;
        resource_set.resource_set = cached->second.resource_set;
        return resource_set.resource_set;
    }

    auto resolved = device.create_resource_set(
        ResourceSetDescription {
            .layout = resource_set.layout,
            .resources = resources,
            .name = resource_set.name,
        }
    );
    auto [cached, _] = m_resource_set_cache.emplace(
        std::move(key),
        CachedResourceSet {
            .resource_set = resolved,
            .layout = resource_set.layout,
            .resources = std::move(resources),
            .last_used_frame = m_frame_index,
        }
    );
    resource_set.resource_set = cached->second.resource_set;
    return resource_set.resource_set;
}

void RenderGraph::release_textures_after_pass(uint32 use_index) {
    for (auto& texture : m_textures) {
        if (texture.last_active_use == use_index) {
            release_texture(texture);
        }
    }
}

void RenderGraph::execute(
    const GraphicsDevice& device,
    CommandBuffer& command_buffer
) {
    if (!m_compiled && !compile()) {
        return;
    }

    m_stats.transient_texture_requests = 0;
    m_stats.transient_texture_hits = 0;
    m_stats.transient_texture_creates = 0;
    update_texture_pool_stats();

    RenderGraphContext context(*this, device, command_buffer);
    for (uint32 use_index = 0; use_index < m_compiled_order.size();
         ++use_index) {
        const auto pass_index = m_compiled_order[use_index];
        auto& pass = *m_passes[pass_index];
        for (const auto& read : pass.reads) {
            ensure_texture_allocated(read.handle, device);
        }
        for (const auto& write : pass.writes) {
            ensure_texture_allocated(write.handle, device);
        }
        pass.execute(context);
        release_textures_after_pass(use_index);
    }

    for (auto& texture : m_textures) {
        if (!texture.imported) {
            release_texture(texture);
        }
    }
    sync_debug_stats();
}

} // namespace fei
