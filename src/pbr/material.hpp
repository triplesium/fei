#pragma once
#include "asset/handle.hpp"
#include "base/optional.hpp"
#include "core/image.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/resource.hpp"
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
    Color3F albedo;
    float metallic;
    float roughness;
};

class StandardMaterial : public Material {
  public:
    ShaderRef vertex_shader() const override {
        return "embeded://forward.vert";
    }
    ShaderRef fragment_shader() const override {
        return "embeded://forward.frag";
    }

    Color3F albedo {1.0f, 1.0f, 1.0f};
    Optional<Handle<Image>> albedo_map;
    Optional<Handle<Image>> normal_map;
    float metallic = 1.0f;
    Optional<Handle<Image>> metallic_map;
    float roughness = 0.5f;
    Optional<Handle<Image>> roughness_map;

    virtual std::vector<ResourceLayoutElementDescription>
    resource_layout_elements() const override {
        return {
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
            {
                .binding = 1,
                .name = "albedo_map",
                .kind = ResourceKind::TextureReadOnly,
                .stages = ShaderStages::Fragment,
            },
            {
                .binding = 2,
                .name = "normal_map",
                .kind = ResourceKind::TextureReadOnly,
                .stages = ShaderStages::Fragment,
            },
            {
                .binding = 3,
                .name = "metallic_map",
                .kind = ResourceKind::TextureReadOnly,
                .stages = ShaderStages::Fragment,
            },
            {
                .binding = 4,
                .name = "roughness_map",
                .kind = ResourceKind::TextureReadOnly,
                .stages = ShaderStages::Fragment,
            }
        };
    }

    StandardMaterialUniform create_uniform() const {
        return StandardMaterialUniform {
            .albedo = albedo,
            .metallic = metallic,
            .roughness = roughness,
        };
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

        auto push_image = [&](const Optional<Handle<Image>>& image_handle) {
            resources.push_back(
                image_handle
                    .transform([&](const Handle<Image>& handle) {
                        return gpu_images.get(handle.id())->texture();
                    })
                    .value_or(defaults.default_texture)
            );
        };

        push_image(albedo_map);
        push_image(normal_map);
        push_image(metallic_map);
        push_image(roughness_map);

        return resources;
    }
};

} // namespace fei
