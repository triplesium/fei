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
    virtual ~ResourceLayoutOpenGL() = default;
    const std::vector<ResourceLayoutElementDescription>& elements() const {
        return m_elements;
    }
};

class ResourceSetOpenGL : public ResourceSet {
  private:
    std::shared_ptr<ResourceLayout> m_layout;
    std::vector<std::shared_ptr<BindableResource>> m_resources;

  public:
    ResourceSetOpenGL(const ResourceSetDescription& desc) :
        ResourceSet(desc), m_layout(desc.layout), m_resources(desc.resources) {}
    virtual ~ResourceSetOpenGL() = default;
    std::shared_ptr<ResourceLayout> layout() const { return m_layout; }
    const std::vector<std::shared_ptr<BindableResource>>& resources() const {
        return m_resources;
    }
};

} // namespace fei
