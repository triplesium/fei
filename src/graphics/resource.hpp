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
};

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
