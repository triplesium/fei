#pragma once

#include "graphics/graphics_device.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fei {

struct RenderResourceSetCacheStats {
    std::uint64_t requests {0};
    std::uint64_t hits {0};
    std::uint64_t creates {0};
    std::size_t size {0};
};

class RenderResourceSetCache {
  private:
    struct ResourceKey {
        const BindableResource* resource {nullptr};
        std::size_t offset {0};
        std::size_t size {BufferRange::WholeSize};

        bool operator==(const ResourceKey&) const = default;
    };

    struct Key {
        const ResourceLayout* layout {nullptr};
        std::vector<ResourceKey> resources;

        bool operator==(const Key&) const = default;
    };

    struct KeyHash {
        std::size_t operator()(const Key& key) const;
    };

    struct Entry {
        std::shared_ptr<ResourceSet> resource_set;
        std::shared_ptr<const ResourceLayout> layout;
        std::vector<std::shared_ptr<const BindableResource>> resources;
        std::uint64_t last_used_frame {0};
    };

    std::unordered_map<Key, Entry, KeyHash> m_entries;
    std::uint64_t m_frame_index {0};
    RenderResourceSetCacheStats m_stats;

    static constexpr std::uint64_t MaxIdleFrames = 120;

  public:
    void begin_frame();
    [[nodiscard]] std::shared_ptr<ResourceSet> get_or_create(
        const GraphicsDevice& device,
        std::string name,
        std::shared_ptr<const ResourceLayout> layout,
        std::vector<std::shared_ptr<const BindableResource>> resources
    );
    void clear();
    [[nodiscard]] const RenderResourceSetCacheStats& stats() const {
        return m_stats;
    }
};

} // namespace fei
