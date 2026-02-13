#pragma once
#include "base/bitflags.hpp"
#include "base/types.hpp"
#include "graphics/enums.hpp"

#include <memory>
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

struct ResourceLayoutElementDescription {
    uint32 binding;
    std::string name;
    ResourceKind kind;
    BitFlags<ShaderStages> stages;
};

struct ResourceLayoutDescription {
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
    ResourceLayout(const ResourceLayoutDescription& desc) {}
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
