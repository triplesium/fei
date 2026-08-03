#include "graphics/enums.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

using namespace fei;

TEST_CASE(
    "Graphics enum helpers describe vertex and pixel sizes",
    "[graphics][enums]"
) {
    REQUIRE(to_vertex_format<float, 4>() == VertexFormat::Float4);
    REQUIRE(to_vertex_format<float, 3>() == VertexFormat::Float3);
    REQUIRE(to_vertex_format<int, 2>() == VertexFormat::Int2);
    REQUIRE(to_vertex_format<std::uint16_t, 4>() == VertexFormat::UShort4);
    REQUIRE(to_vertex_format<std::uint8_t, 4>() == VertexFormat::UByte4);

    REQUIRE(vertex_format_size(VertexFormat::Float) == 4);
    REQUIRE(vertex_format_size(VertexFormat::Float3) == 12);
    REQUIRE(vertex_format_size(VertexFormat::Int4) == 16);
    REQUIRE(vertex_format_size(VertexFormat::UByte4) == 4);

    REQUIRE(get_pixel_format_size(PixelFormat::R8Unorm) == 1);
    REQUIRE(get_pixel_format_size(PixelFormat::Rg8Unorm) == 2);
    REQUIRE(get_pixel_format_size(PixelFormat::Rgba8Unorm) == 4);
    REQUIRE(get_pixel_format_size(PixelFormat::Rgba16Float) == 8);
    REQUIRE(get_pixel_format_size(PixelFormat::Rgba32Float) == 16);
    REQUIRE(get_pixel_format_size(PixelFormat::Bc1RgbaUnorm) == 8);
    REQUIRE(get_pixel_format_size(PixelFormat::Bc7RgbaUnorm) == 16);
}
