#pragma once
#include "graphics/resource.hpp"

#include <memory>
#include <vector>

namespace fei {

class ResourceLayoutVulkan : public ResourceLayout {
  private:
    std::vector<ResourceLayoutElementDescription> m_elements;

  public:
    explicit ResourceLayoutVulkan(const ResourceLayoutDescription& desc) :
        ResourceLayout(desc), m_elements(desc.elements) {}
    ~ResourceLayoutVulkan() override = default;

    const std::vector<ResourceLayoutElementDescription>& elements() const {
        return m_elements;
    }
};

class ResourceSetVulkan : public ResourceSet {
  private:
    std::shared_ptr<ResourceLayout> m_layout;
    std::vector<std::shared_ptr<BindableResource>> m_resources;

  public:
    explicit ResourceSetVulkan(const ResourceSetDescription& desc) :
        ResourceSet(desc), m_layout(desc.layout), m_resources(desc.resources) {}
    ~ResourceSetVulkan() override = default;

    std::shared_ptr<ResourceLayout> layout() const { return m_layout; }
    const std::vector<std::shared_ptr<BindableResource>>& resources() const {
        return m_resources;
    }
};

} // namespace fei
