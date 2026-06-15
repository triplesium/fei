#include "web_preview/frame_cache.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

using namespace fei;

TEST_CASE("WebPreviewFrameCache stores and replaces JPEG frames", "[web_preview]") {
    WebPreviewFrameCache cache;

    REQUIRE(cache.snapshot().empty());

    cache.publish_jpeg(
        std::vector<byte> {byte {0x01}, byte {0x02}},
        320,
        180
    );

    auto first = cache.snapshot();
    REQUIRE_FALSE(first.empty());
    REQUIRE(first.width == 320);
    REQUIRE(first.height == 180);
    REQUIRE(first.index == 1);
    REQUIRE(first.jpeg == std::vector<byte> {byte {0x01}, byte {0x02}});

    cache.publish_jpeg(std::vector<byte> {byte {0x03}}, 640, 360);

    auto second = cache.snapshot();
    REQUIRE(second.width == 640);
    REQUIRE(second.height == 360);
    REQUIRE(second.index == 2);
    REQUIRE(second.jpeg == std::vector<byte> {byte {0x03}});

    cache.clear();

    REQUIRE(cache.snapshot().empty());
}
