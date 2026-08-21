#pragma once
#include "base/result.hpp"
#include "graphics/enums.hpp"
#include "graphics/resource.hpp"
#include "graphics/shader_defs.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace fei {

struct ShaderResourceBinding {
    std::string name;
    std::string backend_name;
    std::vector<std::string> backend_names;
    ResourceKind kind;
    uint32 set;
    uint32 binding;
    uint32 array_size {1};
};

struct ShaderDescription {
    ShaderStages stage;
    std::string source;
    std::string wgsl;
    std::vector<std::byte> spirv;
    std::string path;
    std::vector<ShaderResourceBinding> resources;
    ShaderDefs defs;
};

struct ShaderResourceLayoutError {
    std::string message;
};

Result<std::vector<ResourceLayoutDescription>, ShaderResourceLayoutError>
reflect_resource_layouts(std::span<const ShaderDescription> shaders);

class ShaderModule {
  private:
    ShaderStages m_stage;
    std::string m_path;
    std::vector<ShaderResourceBinding> m_resources;
    ShaderDefs m_defs;

  public:
    ShaderModule(const ShaderDescription& desc) :
        m_stage(desc.stage), m_path(desc.path), m_resources(desc.resources),
        m_defs(desc.defs) {}
    virtual ~ShaderModule() = default;

    ShaderStages stage() const { return m_stage; }
    const std::string& path() const { return m_path; }
    const std::vector<ShaderResourceBinding>& resources() const {
        return m_resources;
    }
    const ShaderDefs& defs() const { return m_defs; }
};

} // namespace fei
