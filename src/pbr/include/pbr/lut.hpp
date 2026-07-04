#pragma once
#include "asset/handle.hpp"
#include "core/image.hpp"
#include "rendering/gpu_image.hpp"

namespace fei {

struct LUTs {
    Handle<Image> brdf_lut;
};

struct GpuLUTs {
    GpuImage brdf_lut;
};

class LUTPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
