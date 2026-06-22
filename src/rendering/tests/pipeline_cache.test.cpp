#include "rendering/pipeline_cache.hpp"

#include "test_graphics_device.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

using namespace fei;
using namespace fei::rendering_test;

TEST_CASE(
    "PipelineCache stores pipelines created by the graphics device",
    "[rendering][pipeline-cache]"
) {
    FakeGraphicsDevice device;
    PipelineCache cache(device);

    RenderPipelineDescription render_description {};
    render_description.render_primitive = RenderPrimitive::Triangles;
    ComputePipelineDescription compute_description {};

    auto render_id = cache.queue_render_pipeline(render_description);
    auto compute_id = cache.queue_compute_pipeline(compute_description);

    REQUIRE(static_cast<std::uint32_t>(render_id) == 0);
    REQUIRE(static_cast<std::uint32_t>(compute_id) == 0);
    REQUIRE(device.render_pipeline_descriptions.size() == 1);
    REQUIRE(device.compute_pipeline_descriptions.size() == 1);
    REQUIRE(
        cache.get_render_pipeline_state(render_id) == CachedPipelineState::Ready
    );
    REQUIRE(
        cache.get_compute_pipeline_state(compute_id) ==
        CachedPipelineState::Ready
    );
    REQUIRE(cache.get_render_pipeline(render_id) == device.render_pipelines[0]);
    REQUIRE(
        cache.get_compute_pipeline(compute_id) == device.compute_pipelines[0]
    );
    REQUIRE(
        cache.get_render_pipeline(static_cast<CachedRenderPipelineId>(99)) ==
        nullptr
    );
    REQUIRE(
        cache.get_render_pipeline_state(
            static_cast<CachedRenderPipelineId>(99)
        ) == CachedPipelineState::Missing
    );
    REQUIRE(
        cache.get_render_pipeline_error(
            static_cast<CachedRenderPipelineId>(99)
        ) == "Render pipeline is missing"
    );
    REQUIRE(
        cache.get_compute_pipeline(static_cast<CachedComputePipelineId>(99)) ==
        nullptr
    );
    REQUIRE(
        cache.get_compute_pipeline_state(
            static_cast<CachedComputePipelineId>(99)
        ) == CachedPipelineState::Missing
    );
    REQUIRE(
        cache.get_compute_pipeline_error(
            static_cast<CachedComputePipelineId>(99)
        ) == "Compute pipeline is missing"
    );
}

TEST_CASE(
    "PipelineCache marks null pipelines as failed",
    "[rendering][pipeline-cache]"
) {
    FakeGraphicsDevice device;
    device.fail_render_pipeline_creation = true;
    device.fail_compute_pipeline_creation = true;
    PipelineCache cache(device);

    auto render_id = cache.queue_render_pipeline(RenderPipelineDescription {});
    auto compute_id =
        cache.queue_compute_pipeline(ComputePipelineDescription {});

    REQUIRE(device.render_pipeline_descriptions.size() == 1);
    REQUIRE(device.compute_pipeline_descriptions.size() == 1);
    REQUIRE(
        cache.get_render_pipeline_state(render_id) ==
        CachedPipelineState::Failed
    );
    REQUIRE(
        cache.get_render_pipeline_error(render_id) ==
        "GraphicsDevice returned null render pipeline"
    );
    REQUIRE(
        cache.get_compute_pipeline_state(compute_id) ==
        CachedPipelineState::Failed
    );
    REQUIRE(
        cache.get_compute_pipeline_error(compute_id) ==
        "GraphicsDevice returned null compute pipeline"
    );
    REQUIRE(cache.get_render_pipeline(render_id) == nullptr);
    REQUIRE(cache.get_compute_pipeline(compute_id) == nullptr);
}
