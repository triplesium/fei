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
    ShaderRef vertex_shader() const override {
        return "embeded://forward.vert";
    }
    ShaderRef fragment_shader() const override {
        return "embeded://forward.frag";
    }
    ShaderRef prepass_vertex_shader() const override {
        return "embeded://deferred_prepass.vert";
    }
    ShaderRef prepass_fragment_shader() const override {
        return "embeded://deferred_prepass.frag";
    }
    ShaderRef deferred_vertex_shader() const override {
        return "embeded://deferred.vert";
    }
    ShaderRef deferred_fragment_shader() const override {
        return "embeded://deferred_gi.frag";
    }

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

    StandardMaterialUniform create_uniform() const {
        BitFlags<StandardMaterialFlags> flags = StandardMaterialFlags::None;
        if (albedo_map.has_value()) {
            flags |= StandardMaterialFlags::AlbedoMap;
        }
        if (normal_map.has_value()) {
            flags |= StandardMaterialFlags::NormalMap;
        }
        if (metallic_map.has_value()) {
            flags |= StandardMaterialFlags::MetallicMap;
        }
        if (roughness_map.has_value()) {
            flags |= StandardMaterialFlags::RoughnessMap;
        }
        if (emissive_map.has_value()) {
            flags |= StandardMaterialFlags::EmissiveMap;
        }
        if (specular_map.has_value()) {
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

    std::vector<std::shared_ptr<BindableResource>> resources(
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
        push_image(emissive_map);
        push_image(specular_map);
        resources.push_back(device.create_sampler(SamplerDescription::Linear));

        return resources;
    }

    std::size_t hash() const override { return type_id<StandardMaterial>(); }
};

} // namespace fei
