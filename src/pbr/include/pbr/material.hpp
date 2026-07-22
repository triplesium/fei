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
    MetallicRoughnessMap = 1u << 2u,
    OcclusionMap = 1u << 3u,
    EmissiveMap = 1u << 4u,
    SpecularMap = 1u << 5u,
    AlphaBlend = 1u << 6u,
    AlbedoSampleLinear = 1u << 7u,
    EmissiveSampleLinear = 1u << 8u,
};

struct alignas(16) StandardMaterialUniform {
    Color3F albedo {1.0f, 1.0f, 1.0f};
    float metallic {0.0f};
    float roughness {0.5f};
    float albedo_alpha {1.0f};
    float alpha_cutoff {0.5f};
    alignas(16) Color3F emissive {0.0f, 0.0f, 0.0f};
    alignas(16) Color3F specular {0.0f, 0.0f, 0.0f};
    uint32 flags {0};
    float normal_scale {1.0f};
    float occlusion_strength {1.0f};
    uint32 albedo_channel {0};
    uint32 normal_channel {0};
    uint32 metallic_roughness_channel {0};
    uint32 occlusion_channel {0};
    uint32 emissive_channel {0};
    uint32 specular_channel {0};
};

enum class UvChannel : uint8 {
    Uv0,
    Uv1,
};

class StandardMaterial : public Material {
  public:
    Color3F albedo {1.0f, 1.0f, 1.0f};
    float albedo_alpha {1.0f};
    float alpha_cutoff {0.5f};
    Optional<Handle<Image>> albedo_texture;
    UvChannel albedo_channel = UvChannel::Uv0;
    Optional<Handle<Image>> normal_texture;
    UvChannel normal_channel = UvChannel::Uv0;
    float normal_scale = 1.0f;
    float metallic = 0.0f;
    float roughness = 0.5f;
    Optional<Handle<Image>> metallic_roughness_texture;
    UvChannel metallic_roughness_channel = UvChannel::Uv0;
    Optional<Handle<Image>> occlusion_texture;
    UvChannel occlusion_channel = UvChannel::Uv0;
    float occlusion_strength = 1.0f;
    Color3F emissive {0.0f, 0.0f, 0.0f};
    Optional<Handle<Image>> emissive_texture;
    UvChannel emissive_channel = UvChannel::Uv0;
    Color3F specular {0.0f, 0.0f, 0.0f};
    Optional<Handle<Image>> specular_texture;
    UvChannel specular_channel = UvChannel::Uv0;
    MaterialAlphaMode alpha_mode {MaterialAlphaMode::Opaque};
    CullMode cull_mode {CullMode::Back};
    bool depth_write {true};

    ShaderRef vertex_shader() const override {
        return ShaderRef("shader://pbr/forward.slang");
    }

    ShaderRef fragment_shader() const override {
        return ShaderRef("shader://pbr/forward.slang");
    }

    ShaderRef prepass_vertex_shader() const override {
        return ShaderRef("shader://pbr/deferred_prepass.slang");
    }

    ShaderRef prepass_fragment_shader() const override {
        return ShaderRef("shader://pbr/deferred_prepass.slang");
    }

    MaterialPipelineState pipeline_state() const override {
        return MaterialPipelineState {
            .alpha_mode = alpha_mode,
            .cull_mode = cull_mode,
            .depth_write = depth_write,
        };
    }

    std::vector<ResourceLayoutElementDescription>
    resource_layout_elements() const override {
        return {
            {
                .binding = 0,
                .name = "material",
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
                .name = "albedo_sampler",
                .kind = ResourceKind::Sampler,
                .stages = ShaderStages::Fragment,
            },
            {
                .binding = 3,
                .name = "normal_map",
                .kind = ResourceKind::TextureReadOnly,
                .stages = ShaderStages::Fragment,
            },
            {
                .binding = 4,
                .name = "normal_sampler",
                .kind = ResourceKind::Sampler,
                .stages = ShaderStages::Fragment,
            },
            {
                .binding = 5,
                .name = "metallic_roughness_map",
                .kind = ResourceKind::TextureReadOnly,
                .stages = ShaderStages::Fragment,
            },
            {
                .binding = 6,
                .name = "metallic_roughness_sampler",
                .kind = ResourceKind::Sampler,
                .stages = ShaderStages::Fragment,
            },
            {
                .binding = 7,
                .name = "occlusion_map",
                .kind = ResourceKind::TextureReadOnly,
                .stages = ShaderStages::Fragment,
            },
            {
                .binding = 8,
                .name = "occlusion_sampler",
                .kind = ResourceKind::Sampler,
                .stages = ShaderStages::Fragment,
            },
            {
                .binding = 9,
                .name = "emissive_map",
                .kind = ResourceKind::TextureReadOnly,
                .stages = ShaderStages::Fragment,
            },
            {
                .binding = 10,
                .name = "emissive_sampler",
                .kind = ResourceKind::Sampler,
                .stages = ShaderStages::Fragment,
            },
            {
                .binding = 11,
                .name = "specular_map",
                .kind = ResourceKind::TextureReadOnly,
                .stages = ShaderStages::Fragment,
            },
            {
                .binding = 12,
                .name = "specular_sampler",
                .kind = ResourceKind::Sampler,
                .stages = ShaderStages::Fragment,
            },
        };
    }

    StandardMaterialUniform
    create_uniform(const RenderAssets<GpuImage>* gpu_images = nullptr) const {
        BitFlags<StandardMaterialFlags> flags = StandardMaterialFlags::None;
        auto image_ready = [&](const Optional<Handle<Image>>& image) {
            return image &&
                   (!gpu_images || gpu_images->get(image->id()).has_value());
        };
        auto sample_is_linear = [&](const Optional<Handle<Image>>& image) {
            if (!gpu_images || !image) {
                return false;
            }
            const auto gpu_image = gpu_images->get(image->id());
            if (!gpu_image) {
                return false;
            }
            switch (gpu_image->texture()->format()) {
                case PixelFormat::Rgba8UnormSrgb:
                case PixelFormat::Bgra8UnormSrgb:
                case PixelFormat::Bc1RgbaUnormSrgb:
                case PixelFormat::Bc2RgbaUnormSrgb:
                case PixelFormat::Bc3RgbaUnormSrgb:
                case PixelFormat::Bc7RgbaUnormSrgb:
                case PixelFormat::Etc2Rgb8UnormSrgb:
                case PixelFormat::Etc2Rgb8A1UnormSrgb:
                case PixelFormat::Etc2Rgba8UnormSrgb:
                    return true;
                default:
                    return false;
            }
        };

        if (image_ready(albedo_texture)) {
            flags |= StandardMaterialFlags::AlbedoMap;
            if (sample_is_linear(albedo_texture)) {
                flags |= StandardMaterialFlags::AlbedoSampleLinear;
            }
        }
        if (image_ready(normal_texture)) {
            flags |= StandardMaterialFlags::NormalMap;
        }
        if (image_ready(metallic_roughness_texture)) {
            flags |= StandardMaterialFlags::MetallicRoughnessMap;
        }
        if (image_ready(occlusion_texture)) {
            flags |= StandardMaterialFlags::OcclusionMap;
        }
        if (image_ready(emissive_texture)) {
            flags |= StandardMaterialFlags::EmissiveMap;
            if (sample_is_linear(emissive_texture)) {
                flags |= StandardMaterialFlags::EmissiveSampleLinear;
            }
        }
        if (image_ready(specular_texture)) {
            flags |= StandardMaterialFlags::SpecularMap;
        }
        if (alpha_mode == MaterialAlphaMode::Blend) {
            flags |= StandardMaterialFlags::AlphaBlend;
        }
        return StandardMaterialUniform {
            .albedo = albedo,
            .metallic = metallic,
            .roughness = roughness,
            .albedo_alpha = albedo_alpha,
            .alpha_cutoff = alpha_cutoff,
            .emissive = emissive,
            .specular = specular,
            .flags = flags.to_raw(),
            .normal_scale = normal_scale,
            .occlusion_strength = occlusion_strength,
            .albedo_channel = static_cast<uint32>(albedo_channel),
            .normal_channel = static_cast<uint32>(normal_channel),
            .metallic_roughness_channel =
                static_cast<uint32>(metallic_roughness_channel),
            .occlusion_channel = static_cast<uint32>(occlusion_channel),
            .emissive_channel = static_cast<uint32>(emissive_channel),
            .specular_channel = static_cast<uint32>(specular_channel),
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

        auto push_image = [&](const Optional<Handle<Image>>& image) {
            if (image) {
                if (auto gpu_image = gpu_images.get(image->id())) {
                    resources.push_back(gpu_image->texture());
                    if (auto sampler = gpu_image->sampler()) {
                        resources.push_back(std::move(sampler));
                    } else {
                        resources.push_back(
                            device.create_sampler(SamplerDescription::Linear)
                        );
                    }
                    return;
                }
            }
            resources.push_back(defaults.default_texture);
            resources.push_back(
                device.create_sampler(SamplerDescription::Linear)
            );
        };

        push_image(albedo_texture);
        push_image(normal_texture);
        push_image(metallic_roughness_texture);
        push_image(occlusion_texture);
        push_image(emissive_texture);
        push_image(specular_texture);

        return resources;
    }

    bool
    resources_ready(const RenderAssets<GpuImage>& gpu_images) const override {
        auto image_ready = [&](const Optional<Handle<Image>>& image) {
            return !image || gpu_images.get(image->id()).has_value();
        };

        return image_ready(albedo_texture) && image_ready(normal_texture) &&
               image_ready(metallic_roughness_texture) &&
               image_ready(occlusion_texture) &&
               image_ready(emissive_texture) && image_ready(specular_texture);
    }

    std::size_t hash() const override { return type_id<StandardMaterial>(); }
};

} // namespace fei
