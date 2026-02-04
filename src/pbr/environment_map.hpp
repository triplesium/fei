#pragma once
#include "asset/handle.hpp"
#include "core/image.hpp"
#include "rendering/gpu_image.hpp"

namespace fei {

struct GeneratedEquirectEnvironmentMap {
    Handle<Image> equirect_image;
};

struct EnvironmentMap {
    Handle<Image> irradiance_cubemap;
    Handle<Image> radiance_cubemap;
};

struct GpuEnvironmentMap {
    enum class EnvMapType { Cubemap, Equirectmap };

    EnvMapType type;
    GpuImage environment_map;
    GpuImage irradiance_cubemap;
    GpuImage radiance_cubemap;
};

struct EnvironmentMapGeneratedTag {};

struct GenerateEnvironmentMapResources {};

class EnvironmentMapPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
