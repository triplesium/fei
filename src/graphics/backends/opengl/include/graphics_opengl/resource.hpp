#pragma once
#include "graphics/resource.hpp"

#include <vector>

namespace fei {

class ResourceLayoutOpenGL : public ResourceLayout {
  private:
    std::vector<ResourceLayoutElementDescription> m_elements;

  public:
    ResourceLayoutOpenGL(const ResourceLayoutDescription& desc) :
        ResourceLayout(desc), m_elements(desc.elements) {}
    ~ResourceLayoutOpenGL() override = default;
    const std::vector<ResourceLayoutElementDescription>& elements() const {
        return m_elements;
    }
};

class ResourceSetOpenGL : public ResourceSet {
  private:
    std::shared_ptr<const ResourceLayout> m_layout;
    std::vector<std::shared_ptr<const BindableResource>> m_resources;

  public:
    ResourceSetOpenGL(const ResourceSetDescription& desc) :
        ResourceSet(desc), m_layout(desc.layout), m_resources(desc.resources) {}
    ~ResourceSetOpenGL() override = default;
    std::shared_ptr<const ResourceLayout> layout() const { return m_layout; }
    const std::vector<std::shared_ptr<const BindableResource>>&
    resources() const {
        return m_resources;
    }
};

} // namespace fei
