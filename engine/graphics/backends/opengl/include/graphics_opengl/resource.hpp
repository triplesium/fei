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
  public:
    ResourceSetOpenGL(const ResourceSetDescription& desc) : ResourceSet(desc) {}
    ~ResourceSetOpenGL() override = default;
    std::shared_ptr<const ResourceLayout> layout() const {
        return resource_layout();
    }
    const std::vector<std::shared_ptr<const BindableResource>>&
    resources() const {
        return bound_resources();
    }
};

} // namespace fei
