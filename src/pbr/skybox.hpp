#pragma once
#include "asset/handle.hpp"
#include "core/image.hpp"
#include "graphics/shader_module.hpp"
#include "rendering/mesh.hpp"

#include <memory>
#include <vector>

namespace fei {

struct SkyboxResource {
    Handle<Mesh> mesh;
    std::vector<std::shared_ptr<ShaderModule>> shader_modules;
};

struct alignas(16) SkyboxUniform {
    Matrix4x4 view_projection;
};

struct Skybox {
    Handle<Image> equirect_map;
};

class SkyboxPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
