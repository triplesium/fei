#include "rendering/resource_set_cache.hpp"

#include <functional>

namespace fei {

namespace {

void hash_combine(std::size_t& seed, std::size_t value) {
    seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

} // namespace

std::size_t RenderResourceSetCache::KeyHash::operator()(const Key& key) const {
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

void RenderResourceSetCache::begin_frame() {
    ++m_frame_index;
    for (auto entry = m_entries.begin(); entry != m_entries.end();) {
        const auto age = m_frame_index >= entry->second.last_used_frame ?
                             m_frame_index - entry->second.last_used_frame :
                             0;
        if (!entry->second.resource_set || age > MaxIdleFrames) {
            entry = m_entries.erase(entry);
        } else {
            ++entry;
        }
    }
    m_stats.size = m_entries.size();
}

std::shared_ptr<ResourceSet> RenderResourceSetCache::get_or_create(
    const GraphicsDevice& device,
    std::string name,
    std::shared_ptr<const ResourceLayout> layout,
    std::vector<std::shared_ptr<const BindableResource>> resources
) {
    ++m_stats.requests;
    if (!layout) {
        return nullptr;
    }

    Key key {.layout = layout.get()};
    key.resources.reserve(resources.size());
    for (const auto& resource : resources) {
        if (!resource) {
            return nullptr;
        }
        if (auto range =
                std::dynamic_pointer_cast<const BufferRange>(resource)) {
            key.resources.push_back(
                ResourceKey {
                    .resource = range->buffer().get(),
                    .offset = range->offset(),
                    .size = range->size(),
                }
            );
        } else {
            key.resources.push_back(ResourceKey {.resource = resource.get()});
        }
    }

    if (auto cached = m_entries.find(key); cached != m_entries.end()) {
        ++m_stats.hits;
        cached->second.last_used_frame = m_frame_index;
        return cached->second.resource_set;
    }

    ++m_stats.creates;
    auto resource_set = device.create_resource_set(
        ResourceSetDescription {
            .layout = layout,
            .resources = resources,
            .name = std::move(name),
        }
    );
    m_entries.emplace(
        std::move(key),
        Entry {
            .resource_set = resource_set,
            .layout = std::move(layout),
            .resources = std::move(resources),
            .last_used_frame = m_frame_index,
        }
    );
    m_stats.size = m_entries.size();
    return resource_set;
}

void RenderResourceSetCache::clear() {
    m_entries.clear();
    m_stats = {};
}

} // namespace fei
