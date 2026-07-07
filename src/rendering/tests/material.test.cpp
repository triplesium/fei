#include "rendering/material.hpp"

#include "asset/assets.hpp"
#include "asset/server.hpp"
#include "ecs/world.hpp"
#include "graphics/buffer.hpp"
#include "rendering/defaults.hpp"
#include "rendering/shader_cache.hpp"
#include "test_graphics_device.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <utility>
#include <vector>

using namespace fei;
using namespace fei::rendering_test;

namespace {

class VariantMaterial : public Material {
  private:
    Handle<Shader> m_vertex_shader;
    Handle<Shader> m_fragment_shader;
    bool m_variant {false};
    MaterialPipelineState m_pipeline_state;

  public:
    VariantMaterial(
        Handle<Shader> vertex_shader,
        Handle<Shader> fragment_shader,
        bool variant,
        MaterialPipelineState pipeline_state = {}
    ) :
        m_vertex_shader(std::move(vertex_shader)),
        m_fragment_shader(std::move(fragment_shader)), m_variant(variant),
        m_pipeline_state(pipeline_state) {}

    ShaderRef vertex_shader() const override {
        return ShaderRef(m_vertex_shader);
    }

    ShaderRef fragment_shader() const override {
        return ShaderRef(m_fragment_shader);
    }

    ShaderDefs shader_defs(MaterialShaderType type) const override {
        if (!m_variant || type != MaterialShaderType::Fragment) {
            return {};
        }
        return {ShaderDefVal::bool_def("TEST_VARIANT")};
    }

    MaterialPipelineState pipeline_state() const override {
        return m_pipeline_state;
    }

    std::vector<ResourceLayoutElementDescription>
    resource_layout_elements() const override {
        return {
            {
                .binding = 0,
                .name = "Material",
                .kind = ResourceKind::UniformBuffer,
                .stages = ShaderStages::Fragment,
            },
        };
    }

    std::vector<std::shared_ptr<const BindableResource>> resources(
        const GraphicsDevice& device,
        const RenderingDefaults&,
        const RenderAssets<GpuImage>&
    ) const override {
        return {
            device.create_buffer(
                BufferDescription {
                    .size = 16,
                    .usages = BufferUsages::Uniform,
                }
            ),
        };
    }

    std::size_t hash() const override { return 7; }
};

Handle<Shader>
add_shader(Assets<Shader>& shaders, ShaderStages stage, std::string path) {
    return shaders.add(
        std::make_unique<Shader>(Shader {
            .path = std::move(path),
            .source = {},
            .spirv = {},
            .stage = stage,
            .resources = {},
        })
    );
}

} // namespace

TEST_CASE(
    "MaterialAdapter includes shader defs and pipeline state in prepared "
    "material hashes",
    "[rendering][material]"
) {
    World world;
    world.add_resource_as<GraphicsDevice>(FakeGraphicsDevice {});
    auto& device =
        dynamic_cast<FakeGraphicsDevice&>(world.resource<GraphicsDevice>());
    world.add_resource(RenderingDefaults {});
    world.add_resource(RenderAssets<GpuImage> {});

    AssetServer asset_server(nullptr);
    Assets<Shader> shaders(nullptr);
    auto vertex_shader = add_shader(shaders, ShaderStages::Vertex, "test.vert");
    auto fragment_shader =
        add_shader(shaders, ShaderStages::Fragment, "test.frag");
    world.add_resource(ShaderCache(asset_server, shaders, device));

    MaterialAdapter<VariantMaterial> adapter;
    auto base_material = adapter.prepare_asset(
        VariantMaterial(vertex_shader, fragment_shader, false),
        world
    );
    auto variant_material = adapter.prepare_asset(
        VariantMaterial(vertex_shader, fragment_shader, true),
        world
    );
    auto pipeline_material = adapter.prepare_asset(
        VariantMaterial(
            vertex_shader,
            fragment_shader,
            false,
            MaterialPipelineState {
                .alpha_mode = MaterialAlphaMode::Blend,
                .cull_mode = CullMode::None,
                .depth_write = false,
            }
        ),
        world
    );

    REQUIRE(base_material);
    REQUIRE(variant_material);
    REQUIRE(pipeline_material);
    CHECK(base_material->hash() != variant_material->hash());
    CHECK(base_material->hash() != pipeline_material->hash());
    REQUIRE(variant_material->shader_request(MaterialShaderType::Fragment));
    CHECK(
        variant_material->shader_request(MaterialShaderType::Fragment)->defs ==
        ShaderDefs {ShaderDefVal::bool_def("TEST_VARIANT")}
    );
    CHECK(
        pipeline_material->pipeline_state().alpha_mode ==
        MaterialAlphaMode::Blend
    );
    CHECK(pipeline_material->pipeline_state().cull_mode == CullMode::None);
    CHECK_FALSE(pipeline_material->pipeline_state().depth_write);
}
