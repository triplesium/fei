#pragma once
#include "asset/handle.hpp"
#include "base/bitflags.hpp"
#include "base/optional.hpp"
#include "core/image.hpp"
#include "ecs/world.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/resource.hpp"
#include "graphics/shader_module.hpp"
#include "math/color.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/shader.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace fei {

class Material {
  public:
    virtual ~Material() = default;
    virtual Handle<Shader> vertex_shader() const = 0;
    virtual Handle<Shader> fragment_shader() const = 0;

    virtual std::vector<ResourceLayoutElementDescription>
    resource_layout_elements() const = 0;

    virtual std::vector<std::shared_ptr<BindableResource>>
    resources(GraphicsDevice& device, World& world) const = 0;

    virtual std::shared_ptr<ResourceSet>
    create_resource_set(GraphicsDevice& device, World& world) const {
        return device.create_resource_set(ResourceSetDescription {
            .layout = create_resource_layout(device, world),
            .resources = resources(device, world),
        });
    }

    virtual std::shared_ptr<ResourceLayout>
    create_resource_layout(GraphicsDevice& device, World& world) const {
        return device.create_resource_layout(ResourceLayoutDescription {
            .elements = resource_layout_elements(),
        });
    }
};

enum class StandardMaterialFlags : std::uint32_t {
    None = 0,
    HasBaseColorTexture = 1 << 0,
};

struct alignas(16) StandardMaterialUniform {
    Color3F base_color {1.0f, 1.0f, 1.0f};
    uint32_t flags {0};
};

class StandardMaterial : public Material {
  private:
    Handle<Shader> m_vertex_shader;
    Handle<Shader> m_fragment_shader;

  public:
    StandardMaterial(
        Handle<Shader> vertex_shader,
        Handle<Shader> fragment_shader
    ) : m_vertex_shader(vertex_shader), m_fragment_shader(fragment_shader) {}

    Handle<Shader> vertex_shader() const override { return m_vertex_shader; }
    Handle<Shader> fragment_shader() const override {
        return m_fragment_shader;
    }

    Color3F base_color {1.0f, 1.0f, 1.0f};
    Optional<Handle<Image>> base_color_texture;

    virtual std::vector<ResourceLayoutElementDescription>
    resource_layout_elements() const override {
        std::vector<ResourceLayoutElementDescription> elements = {
            {
                .binding = 0,
                .name = "Material",
                .kind = ResourceKind::UniformBuffer,
                .stages =
                    {
                        ShaderStages::Vertex,
                        ShaderStages::Fragment,
                    },
            },
        };
        if (base_color_texture) {
            elements.push_back(ResourceLayoutElementDescription {
                .binding = 1,
                .name = "diffuse_texture",
                .kind = ResourceKind::TextureReadOnly,
                .stages = ShaderStages::Fragment,
            });
        }
        return elements;
    }

    StandardMaterialUniform create_uniform() const {
        StandardMaterialUniform uniform;
        uniform.base_color = base_color;
        BitFlags<StandardMaterialFlags> flags;
        if (base_color_texture) {
            flags |= StandardMaterialFlags::HasBaseColorTexture;
        }
        uniform.flags = static_cast<std::uint32_t>(flags.to_raw());
        return uniform;
    }

    virtual std::vector<std::shared_ptr<BindableResource>>
    resources(GraphicsDevice& device, World& world) const override {
        std::vector<std::shared_ptr<BindableResource>> resources;
        auto& gpu_image_assets = world.resource<RenderAssets<GpuImage>>();

        auto uniform = create_uniform();
        auto uniform_buffer = device.create_buffer(BufferDescription {
            .size = sizeof(StandardMaterialUniform),
            .usages = {BufferUsages::Uniform, BufferUsages::Dynamic},
        });
        device.update_buffer(
            uniform_buffer,
            0,
            &uniform,
            sizeof(StandardMaterialUniform)
        );
        resources.push_back(uniform_buffer);

        if (base_color_texture) {
            auto gpu_image = gpu_image_assets.get(base_color_texture->id());
            if (!gpu_image) {
                fatal(
                    "GpuImage for Image asset id {} not found",
                    base_color_texture->id()
                );
            }
            resources.push_back(gpu_image->texture());
        }
        return resources;
    }
};

class PreparedMaterial {
  private:
    std::vector<std::shared_ptr<ShaderModule>> m_shaders;
    std::shared_ptr<ResourceLayout> m_layout;
    std::shared_ptr<ResourceSet> m_resource_set;

  public:
    PreparedMaterial(
        std::vector<std::shared_ptr<ShaderModule>> shaders,
        std::shared_ptr<ResourceLayout> layout,
        std::shared_ptr<ResourceSet> resource_set
    ) :
        m_shaders(std::move(shaders)), m_layout(std::move(layout)),
        m_resource_set(std::move(resource_set)) {}

    const std::vector<std::shared_ptr<ShaderModule>>& shaders() const {
        return m_shaders;
    }
    std::shared_ptr<ResourceLayout> resource_layout() const { return m_layout; }
    std::shared_ptr<ResourceSet> resource_set() const { return m_resource_set; }
};

class PreparedStandardMaterialAdapter
    : public RenderAssetAdapter<StandardMaterial, PreparedMaterial> {
  public:
    Optional<PreparedMaterial>
    prepare_asset(const StandardMaterial& source_asset, World& world) override {
        auto& device = world.resource<GraphicsDevice>();

        auto layout = source_asset.create_resource_layout(device, world);
        if (!layout) {
            return nullopt;
        }

        auto resource_set = device.create_resource_set(ResourceSetDescription {
            .layout = layout,
            .resources = source_asset.resources(device, world),
        });
        if (!resource_set) {
            return nullopt;
        }
        std::vector<Handle<Shader>> shaders = {
            source_asset.vertex_shader(),
            source_asset.fragment_shader(),
        };

        std::vector<std::shared_ptr<ShaderModule>> shader_modules;
        auto& shader_assets = world.resource<Assets<Shader>>();
        for (const auto& shader_handle : shaders) {
            auto shader_asset = shader_assets.get(shader_handle);
            if (!shader_asset) {
                return nullopt;
            }
            auto shader_module = device.create_shader_module(ShaderDescription {
                .stage = shader_asset->stage,
                .source = shader_asset->source,
            });
            if (!shader_module) {
                return nullopt;
            }
            shader_modules.push_back(shader_module);
        }
        return PreparedMaterial {
            std::move(shader_modules),
            std::move(layout),
            std::move(resource_set)
        };
    }
};

} // namespace fei
