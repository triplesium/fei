#pragma once
#include "asset/handle.hpp"
#include "base/optional.hpp"
#include "core/image.hpp"
#include "ecs/world.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/resource.hpp"
#include "graphics/shader_module.hpp"
#include "math/color.hpp"
#include "rendering/defaults.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/material.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/shader.hpp"

#include <memory>
#include <vector>

namespace fei {

struct alignas(16) StandardMaterialUniform {
    Color3F base_color {1.0f, 1.0f, 1.0f};
};

class StandardMaterial : public Material {
  public:
    ShaderRef vertex_shader() const override {
        return "embeded://forward.vert";
    }
    ShaderRef fragment_shader() const override {
        return "embeded://forward.frag";
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
        elements.push_back(ResourceLayoutElementDescription {
            .binding = 1,
            .name = "diffuse_texture",
            .kind = ResourceKind::TextureReadOnly,
            .stages = ShaderStages::Fragment,
        });
        return elements;
    }

    StandardMaterialUniform create_uniform() const {
        StandardMaterialUniform uniform;
        uniform.base_color = base_color;
        return uniform;
    }

    virtual std::vector<std::shared_ptr<BindableResource>> resources(
        GraphicsDevice& device,
        const RenderingDefaults& defaults,
        const RenderAssets<GpuImage>& gpu_images
    ) const override {
        std::vector<std::shared_ptr<BindableResource>> resources;

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

        resources.push_back(
            base_color_texture
                .transform([&](const Handle<Image>& image_handle) {
                    return gpu_images.get(image_handle.id())->texture();
                })
                .value_or(defaults.default_texture)
        );

        return resources;
    }
};

} // namespace fei
