#pragma once
#include "app/plugin.hpp"
#include "graphics/resource.hpp"
#include "graphics/texture.hpp"
#include "pbr/pipeline_specializer.hpp"
#include "rendering/material.hpp"

#include <memory>

namespace fei {

struct DeferedRenderResources {
    std::shared_ptr<Texture> g_position_ao;
    std::shared_ptr<Texture> g_normal_roughness;
    std::shared_ptr<Texture> g_albedo_metallic;
    std::shared_ptr<Texture> g_specular;
    std::shared_ptr<Texture> g_emissive;
    std::shared_ptr<ResourceLayout> defered_resource_layout;
    std::shared_ptr<ResourceSet> defered_resource_set;
    std::shared_ptr<Pipeline> defered_pipeline;
};

class DeferredPipelineSpecializer : public PipelineSpecializer {
  public:
    void specialize(
        RenderPipelineDescription& desc,
        const GpuMesh& mesh,
        const PreparedMaterial& material
    ) const override {
        desc.shader_program.shaders = {
            material.shader(MaterialShaderType::PrepassVertex),
            material.shader(MaterialShaderType::PrepassFragment),
        };
    }
};

class DeferredRenderPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
