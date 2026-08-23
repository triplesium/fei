#pragma once
#include "asset/handle.hpp"
#include "core/image.hpp"
#include "rendering/gpu_image.hpp"

namespace ets {

struct LUTs {
    Handle<Image> brdf_lut;
};

struct GpuLUTs {
    GpuImage brdf_lut;
};

ETS_REFLECT(Plugin)
class LUTPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace ets
