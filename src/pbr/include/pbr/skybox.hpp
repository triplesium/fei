#pragma once
#include "asset/handle.hpp"
#include "core/camera.hpp"
#include "core/image.hpp"
#include "ecs/query.hpp"
#include "ecs/system_params.hpp"
#include "graphics/buffer.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/resource.hpp"
#include "graphics/sampler.hpp"
#include "graphics/shader_module.hpp"
#include "math/matrix.hpp"
#include "math/quaternion.hpp"
#include "math/vector.hpp"
#include "pbr/passes/target.hpp"
#include "rendering/render_frame.hpp"

#include <memory>
#include <vector>

namespace fei {

struct SkyboxResource {
    std::vector<std::shared_ptr<const ShaderModule>> shader_modules;
    std::shared_ptr<ResourceLayout> view_resource_layout;
    std::shared_ptr<ResourceLayout> resource_layout;
    std::shared_ptr<Sampler> sampler;
    std::shared_ptr<Pipeline> pipeline;
};

struct alignas(16) SkyboxUniform {
    Matrix4x4 environment_from_world;
    float brightness {1.0f};
    Vector3 padding {};
};

struct SkyboxViewResourceSet {
    std::shared_ptr<Buffer> uniform_buffer;
    std::shared_ptr<ResourceSet> view_resource_set;
    std::shared_ptr<ResourceSet> resource_set;
    const Buffer* view_buffer {};
    const Texture* texture {};
};

struct Skybox {
    Handle<Image> equirect_map;
    float brightness {1.0f};
    Quaternion rotation {Quaternion::Identity};
};

void render_skybox_pass(
    Query<const Skybox, const SkyboxViewResourceSet>::Filter<With<Camera3d>>
        query,
    ResRW<RenderFrameContext> frame,
    ResRO<RenderTarget> target,
    ResRO<DeferredViewTargets> targets,
    ResRO<SkyboxResource> skybox_resource
);

class SkyboxPlugin : public Plugin {
  public:
    void setup(App& app) override;
};

} // namespace fei
