#pragma once
#include "graphics/sampler.hpp"
#include "graphics/texture.hpp"
#include "rendering/render_graph.hpp"

#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace fei {

struct ShadowMapGraphEntry {
    std::shared_ptr<Texture> texture;
    RgTextureHandle handle;
};

struct ShadowMapGraphHandles {
    std::vector<ShadowMapGraphEntry> entries;

    [[nodiscard]] RgTextureHandle first() const {
        if (entries.empty()) {
            return {};
        }
        return entries.front().handle;
    }
};

struct DeferredGBufferGraphHandles {
    RgTextureHandle position_ao;
    RgTextureHandle normal_roughness;
    RgTextureHandle albedo_metallic;
    RgTextureHandle specular;
    RgTextureHandle emissive_depth;
    RgTextureHandle depth;
};

struct DeferredLightingGraphHandles {
    RgTextureHandle direct;
    RgTextureHandle indirect;
    RgTextureHandle composite;
};

struct VxgiGraphHandles {
    bool imported {false};
    RgTextureHandle albedo;
    RgTextureHandle normal;
    RgTextureHandle emissive;
    RgTextureHandle radiance;
    RgTextureHandle static_flag;
    std::array<RgTextureHandle, 6> mipmap {};
};

inline std::vector<RenderGraphResourceBinding>
deferred_gbuffer_resource_bindings(
    const DeferredGBufferGraphHandles& gbuffer,
    std::shared_ptr<Sampler> point_sampler
) {
    return {
        gbuffer.position_ao,
        gbuffer.normal_roughness,
        gbuffer.albedo_metallic,
        gbuffer.specular,
        gbuffer.emissive_depth,
        std::move(point_sampler),
    };
}

inline std::vector<RenderGraphResourceBinding>
deferred_composite_resource_bindings(
    const DeferredLightingGraphHandles& lighting,
    std::shared_ptr<Sampler> point_sampler
) {
    return {
        lighting.direct,
        lighting.indirect,
        std::move(point_sampler),
    };
}

} // namespace fei
