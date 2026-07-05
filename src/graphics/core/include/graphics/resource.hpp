#pragma once
#include "base/bitflags.hpp"
#include "base/log.hpp"
#include "base/types.hpp"
#include "graphics/enums.hpp"

#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fei {

class Buffer;

enum class ResourceKind : uint8 {
    UniformBuffer,
    TextureReadOnly,
    TextureReadWrite,
    StorageBufferReadOnly,
    StorageBufferReadWrite,
    Sampler,
};

inline std::string_view resource_kind_name(ResourceKind kind) {
    switch (kind) {
        case ResourceKind::UniformBuffer:
            return "uniform buffer";
        case ResourceKind::TextureReadOnly:
            return "read-only texture";
        case ResourceKind::TextureReadWrite:
            return "read-write texture";
        case ResourceKind::StorageBufferReadOnly:
            return "read-only storage buffer";
        case ResourceKind::StorageBufferReadWrite:
            return "read-write storage buffer";
        case ResourceKind::Sampler:
            return "sampler";
    }
    return "unknown resource";
}

enum class ResourceLayoutElementOptions : uint8 {
    None = 0,
    DynamicBinding = 1 << 0,
};

struct ResourceLayoutElementDescription {
    // Binding within the resource set that owns this layout.
    uint32 binding;
    std::string name;
    ResourceKind kind;
    BitFlags<ShaderStages> stages;
    uint32 array_count {1};
    BitFlags<ResourceLayoutElementOptions> options;
};

struct ResourceLayoutDescription {
    // The index of this layout in a pipeline description is its resource set.
    std::vector<ResourceLayoutElementDescription> elements;

    static ResourceLayoutDescription sequencial(
        BitFlags<ShaderStages> stages,
        std::vector<ResourceLayoutElementDescription> resources
    ) {
        ResourceLayoutDescription desc;
        uint32 current_binding = 0;
        for (auto& resource : resources) {
            resource.binding = current_binding++;
            resource.stages = stages;
            desc.elements.push_back(std::move(resource));
        }
        return desc;
    }
};

inline void
validate_resource_layout_description(const ResourceLayoutDescription& desc) {
    for (std::size_t i = 0; i < desc.elements.size(); ++i) {
        if (desc.elements[i].array_count == 0) {
            fei::fatal(
                "ResourceLayout element '{}' has zero array_count",
                desc.elements[i].name
            );
        }
        if (desc.elements[i].options.is_set(
                ResourceLayoutElementOptions::DynamicBinding
            ) &&
            desc.elements[i].kind != ResourceKind::UniformBuffer &&
            desc.elements[i].kind != ResourceKind::StorageBufferReadOnly &&
            desc.elements[i].kind != ResourceKind::StorageBufferReadWrite) {
            fei::fatal(
                "ResourceLayout element '{}' uses DynamicBinding but is {}",
                desc.elements[i].name,
                resource_kind_name(desc.elements[i].kind)
            );
        }
        for (std::size_t j = i + 1; j < desc.elements.size(); ++j) {
            if (desc.elements[i].binding == desc.elements[j].binding) {
                fei::fatal(
                    "ResourceLayout has duplicate binding {} for '{}' and "
                    "'{}'",
                    desc.elements[i].binding,
                    desc.elements[i].name,
                    desc.elements[j].name
                );
            }
        }
    }
}

inline ResourceLayoutElementDescription uniform_buffer(std::string name) {
    return ResourceLayoutElementDescription {
        .binding = 0,
        .name = std::move(name),
        .kind = ResourceKind::UniformBuffer,
        .stages = {},
    };
}

inline ResourceLayoutElementDescription texture_read_only(std::string name) {
    return ResourceLayoutElementDescription {
        .binding = 0,
        .name = std::move(name),
        .kind = ResourceKind::TextureReadOnly,
        .stages = {},
    };
}

inline ResourceLayoutElementDescription texture_read_write(std::string name) {
    return ResourceLayoutElementDescription {
        .binding = 0,
        .name = std::move(name),
        .kind = ResourceKind::TextureReadWrite,
        .stages = {},
    };
}

inline ResourceLayoutElementDescription sampler(std::string name) {
    return ResourceLayoutElementDescription {
        .binding = 0,
        .name = std::move(name),
        .kind = ResourceKind::Sampler,
        .stages = {},
    };
}

inline ResourceLayoutElementDescription
storage_buffer_read_only(std::string name) {
    return ResourceLayoutElementDescription {
        .binding = 0,
        .name = std::move(name),
        .kind = ResourceKind::StorageBufferReadOnly,
        .stages = {},
    };
}

inline ResourceLayoutElementDescription
storage_buffer_read_write(std::string name) {
    return ResourceLayoutElementDescription {
        .binding = 0,
        .name = std::move(name),
        .kind = ResourceKind::StorageBufferReadWrite,
        .stages = {},
    };
}

class ResourceLayout {
  public:
    ResourceLayout(const ResourceLayoutDescription& desc) {
        validate_resource_layout_description(desc);
    }
    virtual ~ResourceLayout() = default;
};

class BindableResource {
  public:
    virtual ~BindableResource() = default;
};

class MappableResource {
  public:
    virtual ~MappableResource() = default;
};

class BufferRange : public BindableResource {
  public:
    static constexpr std::size_t WholeSize =
        std::numeric_limits<std::size_t>::max();

    BufferRange(
        std::shared_ptr<const Buffer> buffer,
        std::size_t offset,
        std::size_t size
    ) : m_buffer(std::move(buffer)), m_offset(offset), m_size(size) {
        if (!m_buffer) {
            fatal("BufferRange requires a buffer");
        }
        if (m_size == 0) {
            fatal("BufferRange requires a non-zero size");
        }
    }

    const std::shared_ptr<const Buffer>& buffer() const { return m_buffer; }
    std::size_t offset() const { return m_offset; }
    std::size_t size() const { return m_size; }

  private:
    std::shared_ptr<const Buffer> m_buffer;
    std::size_t m_offset {0};
    std::size_t m_size {WholeSize};
};

struct ResourceSetDescription {
    std::shared_ptr<const ResourceLayout> layout;
    std::vector<std::shared_ptr<const BindableResource>> resources;
    std::string name;
};

class ResourceSet {
  public:
    ResourceSet(const ResourceSetDescription& desc) {}
    virtual ~ResourceSet() = default;
};

} // namespace fei
