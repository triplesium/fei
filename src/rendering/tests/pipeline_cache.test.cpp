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

    auto render_id = cache.insert_render_pipeline(render_description);
    auto compute_id = cache.insert_compute_pipeline(compute_description);

    REQUIRE(static_cast<std::uint32_t>(render_id) == 0);
    REQUIRE(static_cast<std::uint32_t>(compute_id) == 1);
    REQUIRE(device.render_pipeline_descriptions.size() == 1);
    REQUIRE(device.compute_pipeline_descriptions.size() == 1);
    REQUIRE(cache.get_pipeline(render_id) == device.render_pipelines[0]);
    REQUIRE(cache.get_pipeline(compute_id) == device.compute_pipelines[0]);
    REQUIRE(cache.get_pipeline(static_cast<CachedPipelineId>(99)) == nullptr);
}
