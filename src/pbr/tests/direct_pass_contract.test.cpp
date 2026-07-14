#include "ecs/world.hpp"
#include "pbr/light.hpp"
#include "pbr/passes/deferred_internal.hpp"
#include "pbr/passes/target.hpp"
#include "pbr/vxgi.hpp"
#include "test_graphics_device.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

using namespace fei;
using namespace fei::rendering_test;

namespace {

class ContractCommandBuffer : public CommandBuffer {
  public:
    struct Viewport {
        int32 x {};
        int32 y {};
        uint32 width {};
        uint32 height {};
    };

    std::vector<RenderPassDescription> render_passes;
    std::vector<Viewport> viewports;
    std::vector<std::shared_ptr<const Pipeline>> render_pipelines;
    std::vector<std::shared_ptr<const Pipeline>> compute_pipelines;
    std::vector<std::pair<uint32, std::shared_ptr<const ResourceSet>>>
        resource_sets;
    std::vector<std::tuple<std::size_t, std::size_t, std::size_t>> dispatches;
    uint32 begin_calls {0};
    uint32 end_calls {0};
    uint32 end_render_pass_calls {0};
    uint32 draw_calls {0};

    void begin() override { ++begin_calls; }
    void end() override { ++end_calls; }
    void begin_render_pass(const RenderPassDescription& desc) override {
        render_passes.push_back(desc);
    }
    void end_render_pass() override { ++end_render_pass_calls; }
    void set_viewport(int32 x, int32 y, uint32 width, uint32 height) override {
        viewports.push_back({x, y, width, height});
    }
    void set_scissor(int32, int32, uint32, uint32) override {}
    void set_vertex_buffer(std::shared_ptr<const Buffer>) override {}
    void set_resource_set(
        uint32 slot,
        std::shared_ptr<const ResourceSet> resource_set,
        std::span<const uint32>
    ) override {
        resource_sets.emplace_back(slot, std::move(resource_set));
    }
    void update_buffer(
        std::shared_ptr<Buffer>,
        uint32,
        const void*,
        std::size_t
    ) override {}
    void draw(std::size_t, std::size_t) override { ++draw_calls; }
    void draw_indexed(std::size_t, uint32, int32) override {}
    void dispatch(
        std::size_t group_x,
        std::size_t group_y,
        std::size_t group_z
    ) override {
        dispatches.emplace_back(group_x, group_y, group_z);
    }

  protected:
    void set_render_pipeline_impl(
        std::shared_ptr<const Pipeline> pipeline
    ) override {
        render_pipelines.push_back(std::move(pipeline));
    }
    void set_compute_pipeline_impl(
        std::shared_ptr<const Pipeline> pipeline
    ) override {
        compute_pipelines.push_back(std::move(pipeline));
    }
    void set_index_buffer_impl(
        std::shared_ptr<const Buffer>,
        IndexFormat,
        uint32
    ) override {}
    void generate_mipmaps_impl(std::shared_ptr<const Texture>) override {}
    void copy_texture_impl(
        std::shared_ptr<const Texture>,
        uint32,
        uint32,
        uint32,
        uint32,
        uint32,
        std::shared_ptr<const Texture>,
        uint32,
        uint32,
        uint32,
        uint32,
        uint32,
        uint32,
        uint32,
        uint32,
        uint32
    ) override {}
};

class ContractSwapchain : public Swapchain {
  public:
    std::shared_ptr<const Framebuffer> current_framebuffer;
    uint32 current_width {1280};
    uint32 current_height {720};

    std::shared_ptr<const Framebuffer> framebuffer() const override {
        return current_framebuffer;
    }
    uint32 width() const override { return current_width; }
    uint32 height() const override { return current_height; }
    PixelFormat color_format() const override {
        return PixelFormat::Bgra8Unorm;
    }
    void resize(uint32 width, uint32 height) override {
        current_width = width;
        current_height = height;
    }
    void present() const override {}
};

struct PassTestWorld {
    World world;
    FakeGraphicsDevice* device {nullptr};
    std::shared_ptr<ContractCommandBuffer> commands;

    PassTestWorld() {
        world.add_resource_as<GraphicsDevice>(FakeGraphicsDevice {});
        device = &dynamic_cast<FakeGraphicsDevice&>(
            world.resource<GraphicsDevice>()
        );
        commands = std::make_shared<ContractCommandBuffer>();
        device->next_command_buffer = commands;
        world.add_resource(RenderFrameContext {});
        REQUIRE(world.resource<RenderFrameContext>().begin(*device));
        world.add_resource(RenderResourceSetCache {});
    }
};

std::shared_ptr<Texture> make_texture(
    const GraphicsDevice& device,
    uint32 width,
    uint32 height,
    PixelFormat format,
    BitFlags<TextureUsage> usage,
    TextureType type = TextureType::Texture2D,
    uint32 depth = 1
) {
    return device.create_texture(
        TextureDescription {
            .width = width,
            .height = height,
            .depth = depth,
            .texture_format = format,
            .texture_usage = usage,
            .texture_type = type,
        }
    );
}

VxgiVolumes make_vxgi_volumes(const GraphicsDevice& device) {
    VxgiVolumes volumes;
    volumes.config.voxel_resolution = 64;
    volumes.resource_layout =
        device.create_resource_layout(ResourceLayoutDescription {});
    auto make_volume = [&]() {
        return make_texture(
            device,
            64,
            64,
            PixelFormat::Rgba16Float,
            {TextureUsage::Sampled, TextureUsage::Storage},
            TextureType::Texture3D,
            64
        );
    };
    volumes.albedo = make_volume();
    volumes.normal = make_volume();
    volumes.emissive = make_volume();
    volumes.radiance = make_volume();
    volumes.static_flag = make_volume();
    for (auto& mipmap : volumes.mipmap) {
        mipmap = make_volume();
    }
    return volumes;
}

std::shared_ptr<ContractSwapchain>
setup_present_resources(PassTestWorld& test) {
    test.world.add_resource(
        Window {
            .glfw_window = nullptr,
            .width = 1280,
            .height = 720,
        }
    );
    test.world.add_resource(DeferredViewTargets {});
    test.world.run_system_once(prepare_deferred_view_targets);

    Assets<Mesh> meshes(nullptr);
    auto mesh_handle = meshes.add(MeshFactory::create_quad(2.0f, 2.0f));
    RenderAssets<GpuMesh> gpu_meshes;
    gpu_meshes.insert(
        mesh_handle.id(),
        std::make_unique<GpuMesh>(
            test.device->create_buffer(
                BufferDescription {
                    .size = 64,
                    .usages = BufferUsages::Vertex,
                }
            ),
            nullopt,
            RenderPrimitive::Triangles,
            MeshVertexBufferLayout {
                .attribute_ids = {},
                .layout = VertexBufferLayout(0, VertexStepMode::Vertex, {}),
            },
            0,
            6
        )
    );
    test.world.add_resource(std::move(gpu_meshes));
    test.world.add_resource(FullscreenQuad {mesh_handle});

    auto present_layout =
        test.device->create_resource_layout(ResourceLayoutDescription {});
    auto point_sampler = test.device->create_sampler(SamplerDescription {});
    test.world.add_resource(
        DeferredRenderPipelines {
            .present_resource_layout = std::move(present_layout),
            .point_sampler = std::move(point_sampler),
        }
    );
    test.world.add_resource(PipelineCache(*test.device));
    auto& pipelines = test.world.resource<DeferredRenderPipelines>();
    pipelines.present_composite_pipeline =
        test.world.resource<PipelineCache>().request_render_pipeline(
            RenderPipelineDescription {}
        );
    pipelines.present_composite_pipeline_requested = true;
    test.world.resource<PipelineCache>().process_queued_pipelines();

    auto swapchain = std::make_shared<ContractSwapchain>();
    test.world.add_resource(
        MainSwapchain {
            .swapchain = swapchain,
        }
    );
    return swapchain;
}

} // namespace

TEST_CASE(
    "Deferred prepass records persistent GBuffer and depth attachments",
    "[pbr][pass][contract]"
) {
    PassTestWorld test;
    test.world.add_resource(
        Window {
            .glfw_window = nullptr,
            .width = 1280,
            .height = 720,
        }
    );
    test.world.add_resource(RenderTarget {});
    test.world.add_resource(DeferredViewTargets {});
    test.world.run_system_once(setup_render_target);
    test.world.run_system_once(prepare_deferred_view_targets);
    test.world.add_resource(DeferredPrepassPhase {});
    test.world.add_resource(PipelineCache(*test.device));

    test.world.run_system_once(deferred_prepass);

    REQUIRE(test.commands->render_passes.size() == 1);
    const auto& pass = test.commands->render_passes[0];
    const auto& targets = test.world.resource<DeferredViewTargets>();
    const auto& target = test.world.resource<RenderTarget>();
    REQUIRE(pass.color_attachments.size() == 5);
    CHECK(pass.color_attachments[0].texture == targets.position_ao);
    CHECK(pass.color_attachments[1].texture == targets.normal_roughness);
    CHECK(pass.color_attachments[2].texture == targets.albedo_metallic);
    CHECK(pass.color_attachments[3].texture == targets.specular);
    CHECK(pass.color_attachments[4].texture == targets.emissive_depth);
    REQUIRE(pass.depth_stencil_attachment);
    CHECK(pass.depth_stencil_attachment->texture == target.depth_texture);
    CHECK(pass.depth_stencil_attachment->clear_depth == 1.0f);
    REQUIRE(test.commands->viewports.size() == 1);
    CHECK(test.commands->viewports[0].width == 1280);
    CHECK(test.commands->viewports[0].height == 720);
    CHECK(test.commands->end_render_pass_calls == 1);
}

TEST_CASE(
    "Shadow pass records physical color and depth attachments",
    "[pbr][pass][contract]"
) {
    PassTestWorld test;
    auto shadow = make_texture(
        *test.device,
        1024,
        1024,
        PixelFormat::Rgba32Float,
        {TextureUsage::RenderTarget, TextureUsage::Sampled}
    );
    auto depth = make_texture(
        *test.device,
        1024,
        1024,
        PixelFormat::Depth32Float,
        TextureUsage::DepthStencil
    );
    ShadowMapPhase phase;
    phase.passes.push_back(
        ShadowMapPhase::Pass {
            .texture = shadow,
        }
    );
    test.world.add_resource(std::move(phase));
    test.world.add_resource(
        ShadowMappingResources {
            .pipeline_specializer = ShadowMapPipelineSpecializer({}),
            .temp_depth_texture = depth,
        }
    );
    test.world.add_resource(PipelineCache(*test.device));

    test.world.run_system_once(render_shadow_map_passes);

    REQUIRE(test.commands->render_passes.size() == 1);
    const auto& pass = test.commands->render_passes[0];
    REQUIRE(pass.color_attachments.size() == 1);
    CHECK(pass.color_attachments[0].texture == shadow);
    REQUIRE(pass.depth_stencil_attachment);
    CHECK(pass.depth_stencil_attachment->texture == depth);
    CHECK(pass.depth_stencil_attachment->texture->usage().is_set(
        TextureUsage::DepthStencil
    ));
    REQUIRE(test.commands->viewports.size() == 1);
    CHECK(test.commands->viewports[0].width == 1024);
    CHECK(test.commands->viewports[0].height == 1024);
    CHECK(test.commands->end_render_pass_calls == 1);
}

TEST_CASE(
    "VXGI compute passes bind cached sets and preserve explicit call order",
    "[pbr][pass][contract]"
) {
    PassTestWorld test;
    auto volumes = make_vxgi_volumes(*test.device);
    auto base_pipeline = std::make_shared<FakePipeline>();
    auto propagation_pipeline = std::make_shared<FakePipeline>();
    auto empty_layout =
        test.device->create_resource_layout(ResourceLayoutDescription {});
    auto uniform = test.device->create_buffer(
        BufferDescription {.size = 64, .usages = BufferUsages::Uniform}
    );
    std::array<std::shared_ptr<TextureView>, 6> output_views;
    for (std::size_t index = 0; index < output_views.size(); ++index) {
        output_views[index] = test.device->create_texture_view(
            TextureViewDescription {
                .target = volumes.mipmap[index],
                .base_mip_level = 0,
                .mip_levels = 1,
            }
        );
    }
    test.world.add_resource(std::move(volumes));
    test.world.add_resource(
        VxgiGenerateMipmapBase {
            .pipeline = base_pipeline,
            .resource_layout = empty_layout,
            .uniform_buffer = uniform,
            .output_views = std::move(output_views),
        }
    );
    test.world.add_resource(
        VxgiInjectPropagation {
            .pipeline = propagation_pipeline,
            .resource_layout = empty_layout,
            .uniform_buffer = uniform,
            .voxel_sampler = test.device->create_sampler(SamplerDescription {}),
        }
    );

    test.world.run_system_once(render_vxgi_mipmap_base_pass);
    test.world.run_system_once(render_vxgi_inject_propagation_pass);
    test.world.run_system_once(render_vxgi_mipmap_base_after_propagation_pass);

    REQUIRE(test.commands->compute_pipelines.size() == 3);
    CHECK(test.commands->compute_pipelines[0] == base_pipeline);
    CHECK(test.commands->compute_pipelines[1] == propagation_pipeline);
    CHECK(test.commands->compute_pipelines[2] == base_pipeline);
    REQUIRE(test.commands->dispatches.size() == 3);
    CHECK(test.commands->dispatches[0] == std::tuple {4ULL, 4ULL, 4ULL});
    CHECK(test.commands->dispatches[1] == std::tuple {8ULL, 8ULL, 8ULL});
    CHECK(test.commands->dispatches[2] == std::tuple {4ULL, 4ULL, 4ULL});
    REQUIRE(test.commands->resource_sets.size() == 3);
    CHECK(test.commands->resource_sets[0].first == 0);
    CHECK(test.commands->resource_sets[1].first == 0);
    CHECK(test.commands->resource_sets[2].first == 0);
    CHECK(test.device->resource_set_descriptions.size() == 2);
    CHECK(test.world.resource<RenderResourceSetCache>().stats().hits == 1);
}

TEST_CASE(
    "Present pass skips a suspended swapchain and resumes with a framebuffer",
    "[pbr][pass][contract][present]"
) {
    PassTestWorld test;
    auto swapchain = setup_present_resources(test);

    test.world.run_system_once(present_composite_pass);
    CHECK(test.commands->render_passes.empty());
    CHECK(test.commands->draw_calls == 0);

    auto swapchain_texture = make_texture(
        *test.device,
        swapchain->width(),
        swapchain->height(),
        PixelFormat::Bgra8Unorm,
        TextureUsage::RenderTarget
    );
    swapchain->current_framebuffer = test.device->create_framebuffer(
        FramebufferDescription {
            .color_targets = {
                FramebufferAttachment {.texture = swapchain_texture},
            },
        }
    );

    test.world.run_system_once(present_composite_pass);

    REQUIRE(test.commands->render_passes.size() == 1);
    const auto& pass = test.commands->render_passes.front();
    CHECK(pass.framebuffer == swapchain->current_framebuffer);
    REQUIRE(pass.color_attachments.size() == 1);
    CHECK(pass.color_attachments[0].load_op == LoadOp::Clear);
    REQUIRE(test.commands->viewports.size() == 1);
    CHECK(test.commands->viewports[0].width == 1280);
    CHECK(test.commands->viewports[0].height == 720);
    CHECK(test.commands->render_pipelines.size() == 1);
    CHECK(test.commands->draw_calls == 1);
    CHECK(test.commands->end_render_pass_calls == 1);
}

TEST_CASE(
    "Present pass keeps clear behavior while its pipeline is unavailable",
    "[pbr][pass][contract][present]"
) {
    PassTestWorld test;
    auto swapchain = setup_present_resources(test);
    auto swapchain_texture = make_texture(
        *test.device,
        1280,
        720,
        PixelFormat::Bgra8Unorm,
        TextureUsage::RenderTarget
    );
    swapchain->current_framebuffer = test.device->create_framebuffer(
        FramebufferDescription {
            .color_targets = {
                FramebufferAttachment {.texture = swapchain_texture},
            },
        }
    );
    test.world.resource<DeferredRenderPipelines>()
        .present_composite_pipeline_requested = false;

    test.world.run_system_once(present_composite_pass);

    REQUIRE(test.commands->render_passes.size() == 1);
    CHECK(test.commands->render_pipelines.empty());
    CHECK(test.commands->draw_calls == 0);
    CHECK(test.commands->end_render_pass_calls == 1);
}
