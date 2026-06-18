#pragma once
#include "base/bitflags.hpp"
#include "base/log.hpp"
#include "base/types.hpp"
#include "graphics/enums.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fei {

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

struct ResourceLayoutElementDescription {
    // Binding within the resource set that owns this layout.
    uint32 binding;
    std::string name;
    ResourceKind kind;
    BitFlags<ShaderStages> stages;
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

struct ResourceSetDescription {
    std::shared_ptr<ResourceLayout> layout;
    std::vector<std::shared_ptr<BindableResource>> resources;
};

class ResourceSet {
  public:
    ResourceSet(const ResourceSetDescription& desc) {}
    virtual ~ResourceSet() = default;
};

} // namespace fei
