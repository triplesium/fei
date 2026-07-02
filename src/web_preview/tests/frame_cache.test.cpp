#include "web_preview/frame_cache.hpp"

#include "web_preview/frame_encoder.hpp"
#include "web_preview/web_input.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace fei;

TEST_CASE(
    "WebPreviewFrameCache stores and replaces JPEG frames",
    "[web_preview]"
) {
    WebPreviewFrameCache cache;

    REQUIRE(cache.snapshot().empty());

    cache.publish_jpeg(
        std::vector<byte> {byte {0x01}, byte {0x02}},
        320,
        180,
        "test.target"
    );

    auto first = cache.snapshot();
    REQUIRE_FALSE(first.empty());
    REQUIRE(first.width == 320);
    REQUIRE(first.height == 180);
    REQUIRE(first.index == 1);
    REQUIRE(first.target == "test.target");
    REQUIRE(first.jpeg == std::vector<byte> {byte {0x01}, byte {0x02}});
    REQUIRE(
        cache.wait_for_frame_after(0, std::chrono::milliseconds {1}).index == 1
    );
    REQUIRE(
        cache.wait_for_frame_after(1, std::chrono::milliseconds {1}).empty()
    );

    auto first_status = cache.status();
    REQUIRE(first_status.has_frame);
    REQUIRE(first_status.width == 320);
    REQUIRE(first_status.height == 180);
    REQUIRE(first_status.frame_index == 1);
    REQUIRE(first_status.jpeg_bytes == 2);
    REQUIRE(first_status.capture_fps == 0.0f);
    REQUIRE(first_status.engine_fps == 0.0f);
    REQUIRE(first_status.target == "test.target");
    REQUIRE(first_status.last_error.empty());

    cache.update_debug_stats(
        WebPreviewDebugStats {
            .render_graph =
                WebPreviewRenderGraphStats {
                    .compiled = true,
                    .total_passes = 7,
                    .active_passes = 5,
                    .culled_passes = 2,
                    .transient_texture_requests = 4,
                    .transient_texture_hits = 3,
                    .transient_texture_creates = 1,
                    .texture_pool_size = 6,
                    .active_order = {"prepass", "present"},
                },
            .render_graph_passes =
                {
                    WebPreviewRenderGraphPass {
                        .index = 0,
                        .name = "prepass",
                        .active = true,
                    },
                },
            .render_graph_textures =
                {
                    WebPreviewRenderGraphTexture {
                        .index = 0,
                        .name = "color",
                        .active = true,
                        .width = 320,
                        .height = 180,
                        .format = "Rgba8Unorm",
                    },
                },
            .render_graph_resource_sets =
                {
                    WebPreviewRenderGraphResourceSet {
                        .index = 0,
                        .name = "gbuffer",
                        .active = true,
                        .resolved = true,
                    },
                },
            .graphics_cache = WebPreviewGraphicsCacheStats {
                .framebuffer_requests = 10,
                .framebuffer_hits = 8,
                .framebuffer_creates = 2,
                .resource_set_requests = 12,
                .resource_set_hits = 9,
                .resource_set_creates = 3,
                .framebuffer_cache_size = 4,
                .resource_set_cache_size = 5,
                .resource_set_sources = {
                    WebPreviewResourceSetSourceStats {
                        .name = "deferred.gbuffer",
                        .requests = 6,
                        .hits = 4,
                        .creates = 2,
                        .cache_size = 1,
                    },
                },
            },
        }
    );
    auto debug_status = cache.status();
    REQUIRE(debug_status.debug.render_graph.total_passes == 7);
    REQUIRE(debug_status.debug.render_graph.active_passes == 5);
    REQUIRE(debug_status.debug.render_graph.texture_pool_size == 6);
    REQUIRE(debug_status.debug.render_graph.compiled);
    REQUIRE(debug_status.debug.render_graph.active_order.size() == 2);
    REQUIRE(debug_status.debug.render_graph_passes.size() == 1);
    REQUIRE(debug_status.debug.render_graph_passes[0].name == "prepass");
    REQUIRE(debug_status.debug.render_graph_textures.size() == 1);
    REQUIRE(debug_status.debug.render_graph_textures[0].name == "color");
    REQUIRE(debug_status.debug.render_graph_resource_sets.size() == 1);
    REQUIRE(debug_status.debug.render_graph_resource_sets[0].resolved);
    REQUIRE(debug_status.debug.graphics_cache.framebuffer_requests == 10);
    REQUIRE(debug_status.debug.graphics_cache.framebuffer_hits == 8);
    REQUIRE(debug_status.debug.graphics_cache.resource_set_cache_size == 5);
    REQUIRE(debug_status.debug.graphics_cache.resource_set_sources.size() == 1);
    REQUIRE(
        debug_status.debug.graphics_cache.resource_set_sources[0].name ==
        "deferred.gbuffer"
    );

    cache.mark_frame_tick();
    REQUIRE(cache.status().engine_fps == 0.0f);

    std::this_thread::sleep_for(std::chrono::milliseconds {1});
    cache.mark_frame_tick();
    REQUIRE(cache.status().engine_fps > 0.0f);

    cache.report_failure("readback failed");
    REQUIRE(cache.status().last_error == "readback failed");

    std::this_thread::sleep_for(std::chrono::milliseconds {1});
    cache.publish_jpeg(std::vector<byte> {byte {0x03}}, 640, 360, "new.target");

    auto second = cache.snapshot();
    REQUIRE(second.width == 640);
    REQUIRE(second.height == 360);
    REQUIRE(second.index == 2);
    REQUIRE(second.target == "new.target");
    REQUIRE(second.jpeg == std::vector<byte> {byte {0x03}});
    REQUIRE(cache.status().capture_fps > 0.0f);
    REQUIRE(cache.status().engine_fps > 0.0f);
    REQUIRE(cache.status().last_error.empty());

    cache.clear();

    REQUIRE(cache.snapshot().empty());
    REQUIRE_FALSE(cache.status().has_frame);
    REQUIRE(cache.status().capture_fps == 0.0f);
    REQUIRE(cache.status().engine_fps == 0.0f);
    REQUIRE(cache.status().debug.render_graph.total_passes == 0);
    REQUIRE(cache.status().debug.render_graph_passes.empty());
}

TEST_CASE(
    "WebPreviewFrameEncoder publishes JPEG frames on a worker thread",
    "[web_preview]"
) {
    auto cache = std::make_shared<WebPreviewFrameCache>();
    WebPreviewFrameEncoder encoder(cache);
    encoder.start();

    REQUIRE(encoder.can_accept_frame());
    REQUIRE(encoder.submit(
        WebPreviewEncodeJob {
            .rgba =
                {
                    byte {255},
                    byte {0},
                    byte {0},
                    byte {255},
                    byte {0},
                    byte {255},
                    byte {0},
                    byte {255},
                    byte {0},
                    byte {0},
                    byte {255},
                    byte {255},
                    byte {255},
                    byte {255},
                    byte {255},
                    byte {255},
                },
            .width = 2,
            .height = 2,
            .jpeg_quality = 80,
            .target = "encoder.test",
        }
    ));

    for (auto attempt = 0; attempt < 100 && cache->snapshot().empty();
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds {10});
    }

    auto frame = cache->snapshot();
    encoder.stop();

    REQUIRE_FALSE(frame.empty());
    REQUIRE(frame.width == 2);
    REQUIRE(frame.height == 2);
    REQUIRE(frame.index == 1);
    REQUIRE(frame.target == "encoder.test");
    REQUIRE(!frame.jpeg.empty());
    REQUIRE(cache->status().last_error.empty());
}

TEST_CASE("WebPreviewInput tracks pressed web keys", "[web_preview]") {
    WebPreviewInput input;

    REQUIRE(input.pressed_keys().empty());
    REQUIRE_FALSE(input.set_key(KeyCode::Unknown, true));

    REQUIRE(input.set_key(KeyCode::W, true));
    REQUIRE(input.set_key(KeyCode::W, true));
    REQUIRE(input.set_key(KeyCode::Space, true));

    auto keys = input.pressed_keys();
    REQUIRE(keys == std::vector<KeyCode> {KeyCode::W, KeyCode::Space});

    REQUIRE(input.set_key(KeyCode::W, false));
    REQUIRE(input.pressed_keys() == std::vector<KeyCode> {KeyCode::Space});

    input.clear();
    REQUIRE(input.pressed_keys().empty());
}
