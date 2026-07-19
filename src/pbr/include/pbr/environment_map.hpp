#pragma once
#include "asset/handle.hpp"
#include "core/image.hpp"
#include "ecs/query.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/resource.hpp"
#include "graphics/sampler.hpp"
#include "math/matrix.hpp"
#include "math/quaternion.hpp"
#include "math/vector.hpp"
#include "rendering/gpu_image.hpp"

#include <memory>
#include <unordered_map>

namespace fei {

struct GeneratedEquirectEnvironmentMap {
    Handle<Image> equirect_image;
};

struct EnvironmentMap {
    AssetId source_equirect_image {invalid_asset_id};
    Handle<Image> irradiance_cubemap;
    Handle<Image> radiance_cubemap;
};

struct GpuEnvironmentMap {
    GpuImage environment_cubemap;
    GpuImage irradiance_cubemap;
    GpuImage radiance_cubemap;
};

struct EnvironmentMapLight {
    float intensity {1.0f};
    Quaternion rotation {Quaternion::Identity};
};

struct alignas(16) EnvironmentMapUniform {
    Matrix4x4 environment_from_world;
    float intensity {1.0f};
    float max_specular_lod {0.0f};
    Vector2 padding {};
};

struct EnvironmentMapGenerationResources {
    std::shared_ptr<ResourceLayout> irradiance_layout;
    std::shared_ptr<ResourceLayout> radiance_layout;
    std::shared_ptr<Pipeline> irradiance_pipeline;
    std::shared_ptr<Pipeline> radiance_pipeline;
    std::shared_ptr<Sampler> cubemap_sampler;

    [[nodiscard]] bool valid() const {
        return irradiance_layout && radiance_layout && irradiance_pipeline &&
               radiance_pipeline && cubemap_sampler;
    }
};

struct EnvironmentMapCache {
    struct Entry {
        EnvironmentMap environment_map;
        const Texture* generated_from_texture {};
    };

    std::unordered_map<AssetId, Entry> entries;
};

void generate_env_maps(
    Query<const GeneratedEquirectEnvironmentMap, const GpuEnvironmentMap> query,
    ResRO<GraphicsDevice> device,
    ResRO<EnvironmentMapGenerationResources> resources,
    ResRW<EnvironmentMapCache> cache
);

class EnvironmentMapPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
