#pragma once
#include "asset/handle.hpp"
#include "base/bitflags.hpp"
#include "base/optional.hpp"
#include "core/image.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/resource.hpp"
#include "graphics/sampler.hpp"
#include "math/color.hpp"
#include "refl/type.hpp"
#include "rendering/defaults.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/material.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/shader.hpp"

#include <memory>
#include <vector>

namespace fei {

enum class StandardMaterialFlags : uint32 {
    None = 0u,
    AlbedoMap = 1u << 0u,
    NormalMap = 1u << 1u,
    MetallicMap = 1u << 2u,
    RoughnessMap = 1u << 3u,
    EmissiveMap = 1u << 4u,
    SpecularMap = 1u << 5u,
};

struct alignas(16) StandardMaterialUniform {
    Color3F albedo {1.0f, 1.0f, 1.0f};
    float metallic {0.0f};
    float roughness {0.5f};
    alignas(16) Color3F emissive {0.0f, 0.0f, 0.0f};
    alignas(16) Color3F specular {0.0f, 0.0f, 0.0f};
    uint32 flags {0};
};

class StandardMaterial : public Material {
  public:
    Color3F albedo {1.0f, 1.0f, 1.0f};
    Optional<Handle<Image>> albedo_map;
    Optional<Handle<Image>> normal_map;
    float metallic = 0.0f;
    Optional<Handle<Image>> metallic_map;
    float roughness = 0.5f;
    Optional<Handle<Image>> roughness_map;
    Color3F emissive {0.0f, 0.0f, 0.0f};
    Optional<Handle<Image>> emissive_map;
    Color3F specular {0.0f, 0.0f, 0.0f};
    Optional<Handle<Image>> specular_map;

    std::vector<ResourceLayoutElementDescription>
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
            },
            {
                .binding = 5,
                .name = "emissive_map",
                .kind = ResourceKind::TextureReadOnly,
                .stages = ShaderStages::Fragment,
            },
            {
                .binding = 6,
                .name = "specular_map",
                .kind = ResourceKind::TextureReadOnly,
                .stages = ShaderStages::Fragment,
            },
            {
                .binding = 7,
                .name = "sampler",
                .kind = ResourceKind::Sampler,
                .stages = ShaderStages::Fragment,
            },
        };
    }

    StandardMaterialUniform
    create_uniform(const RenderAssets<GpuImage>* gpu_images = nullptr) const {
        BitFlags<StandardMaterialFlags> flags = StandardMaterialFlags::None;
        auto image_ready = [&](const Optional<Handle<Image>>& image_handle) {
            return image_handle &&
                   (!gpu_images ||
                    gpu_images->get(image_handle->id()).has_value());
        };

        if (image_ready(albedo_map)) {
            flags |= StandardMaterialFlags::AlbedoMap;
        }
        if (image_ready(normal_map)) {
            flags |= StandardMaterialFlags::NormalMap;
        }
        if (image_ready(metallic_map)) {
            flags |= StandardMaterialFlags::MetallicMap;
        }
        if (image_ready(roughness_map)) {
            flags |= StandardMaterialFlags::RoughnessMap;
        }
        if (image_ready(emissive_map)) {
            flags |= StandardMaterialFlags::EmissiveMap;
        }
        if (image_ready(specular_map)) {
            flags |= StandardMaterialFlags::SpecularMap;
        }
        return StandardMaterialUniform {
            .albedo = albedo,
            .metallic = metallic,
            .roughness = roughness,
            .emissive = emissive,
            .specular = specular,
            .flags = flags.to_raw(),
        };
    }

    std::vector<std::shared_ptr<const BindableResource>> resources(
        const GraphicsDevice& device,
        const RenderingDefaults& defaults,
        const RenderAssets<GpuImage>& gpu_images
    ) const override {
        std::vector<std::shared_ptr<const BindableResource>> resources;

        auto uniform = create_uniform(&gpu_images);
        auto uniform_buffer = device.create_buffer(
            BufferDescription {
                .size = sizeof(StandardMaterialUniform),
                .usages = {BufferUsages::Uniform, BufferUsages::Dynamic},
            }
        );
        device.update_buffer(
            uniform_buffer,
            0,
            &uniform,
            sizeof(StandardMaterialUniform)
        );
        resources.push_back(uniform_buffer);

        auto push_image = [&](const Optional<Handle<Image>>& image_handle) {
            if (image_handle) {
                if (auto gpu_image = gpu_images.get(image_handle->id())) {
                    resources.push_back(gpu_image->texture());
                    return;
                }
            }
            resources.push_back(defaults.default_texture);
        };

        push_image(albedo_map);
        push_image(normal_map);
        push_image(metallic_map);
        push_image(roughness_map);
        push_image(emissive_map);
        push_image(specular_map);
        resources.push_back(device.create_sampler(SamplerDescription::Linear));

        return resources;
    }

    bool
    resources_ready(const RenderAssets<GpuImage>& gpu_images) const override {
        auto image_ready = [&](const Optional<Handle<Image>>& image_handle) {
            return !image_handle ||
                   gpu_images.get(image_handle->id()).has_value();
        };

        return image_ready(albedo_map) && image_ready(normal_map) &&
               image_ready(metallic_map) && image_ready(roughness_map) &&
               image_ready(emissive_map) && image_ready(specular_map);
    }

    std::size_t hash() const override { return type_id<StandardMaterial>(); }
};

} // namespace fei
