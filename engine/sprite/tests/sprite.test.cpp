#include "ecs/type_tags.hpp"
#include "graphics/resource.hpp"
#include "math/common.hpp"
#include "refl/generated.hpp"
#include "refl/registry.hpp"
#include "sprite/components.hpp"
#include "sprite/output.hpp"
#include "sprite/renderer.hpp"
#include "test_graphics_device.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>

using namespace fei;
using namespace fei::rendering_test;

namespace {

void check_near(float actual, float expected) {
    CHECK(actual == Catch::Approx(expected).margin(EPSILON));
}

void check_position(const Vector2& actual, float x, float y) {
    check_near(actual.x, x);
    check_near(actual.y, y);
}

} // namespace

TEST_CASE(
    "Generated reflection tags 2D rendering components",
    "[sprite][refl][tag]"
) {
    register_generated_reflection();
    auto& registry = Registry::instance();

    CHECK(registry.get_type<Camera2d>().has_tag(ComponentTypeTag));
    CHECK(registry.get_type<Sprite>().has_tag(ComponentTypeTag));
}

TEST_CASE("Camera2d projection preserves vertical size", "[sprite][camera]") {
    const Camera2d camera {.vertical_size = 10.0f};
    const Transform2d transform {.position = {2.0f, 3.0f}};
    const auto clip_from_world =
        camera_2d_clip_from_world(camera, transform, 200, 100);

    const auto center = clip_from_world * Vector4 {2.0f, 3.0f, 0.0f, 1.0f};
    const auto top = clip_from_world * Vector4 {2.0f, 8.0f, 0.0f, 1.0f};
    const auto right = clip_from_world * Vector4 {12.0f, 3.0f, 0.0f, 1.0f};

    check_near(center.x, 0.0f);
    check_near(center.y, 0.0f);
    check_near(top.y, 1.0f);
    check_near(right.x, 1.0f);
}

TEST_CASE("Sprite quad applies size and transform", "[sprite][geometry]") {
    const Sprite sprite {
        .size = {2.0f, 4.0f},
        .color = {0.25f, 0.5f, 0.75f, 0.8f},
    };
    const Transform2d transform {
        .position = {3.0f, 2.0f},
        .scale = {2.0f, 0.5f},
        .rotation = 0.0f,
    };

    const auto quad = make_sprite_quad(sprite, transform);

    check_position(quad.vertices[0].position, 1.0f, 1.0f);
    check_position(quad.vertices[1].position, 5.0f, 1.0f);
    check_position(quad.vertices[2].position, 1.0f, 3.0f);
    check_position(quad.vertices[3].position, 5.0f, 3.0f);
    CHECK(quad.vertices[0].uv == Vector2 {0.0f, 0.0f});
    CHECK(quad.vertices[3].uv == Vector2 {1.0f, 1.0f});
    CHECK(quad.vertices[0].color.a == Catch::Approx(0.8f));
    CHECK(quad.indices == std::array<std::uint32_t, 6> {0, 1, 2, 2, 1, 3});
}

TEST_CASE(
    "Sprite quad accepts propagated world transforms",
    "[sprite][geometry]"
) {
    const Sprite sprite {.size = {2.0f, 2.0f}};
    const auto parent = Transform2d {.position = {3.0f, 4.0f}}.model_matrix();
    const auto local = Transform2d {.position = {1.0f, 2.0f}}.model_matrix();

    const auto quad = make_sprite_quad(sprite, parent * local);

    check_position(quad.vertices[0].position, 3.0f, 5.0f);
    check_position(quad.vertices[3].position, 5.0f, 7.0f);
}

TEST_CASE(
    "Sprite output creates and resizes a sampled render texture",
    "[sprite][output]"
) {
    FakeGraphicsDevice device;
    SpriteOutput output {
        .mode = SpriteOutputMode::Texture,
        .requested_width = 320,
        .requested_height = 180,
    };

    update_sprite_output(device, nullptr, output);

    REQUIRE(output.texture);
    REQUIRE(output.framebuffer);
    CHECK(output.width == 320);
    CHECK(output.height == 180);
    CHECK(output.texture->format() == PixelFormat::Rgba8Unorm);
    CHECK(output.texture->usage().is_set(TextureUsage::RenderTarget));
    CHECK(output.texture->usage().is_set(TextureUsage::Sampled));
    REQUIRE(output.framebuffer->color_attachments().size() == 1);
    CHECK(output.framebuffer->color_attachments()[0].texture == output.texture);

    const auto first_texture = output.texture;
    update_sprite_output(device, nullptr, output);
    CHECK(output.texture == first_texture);
    REQUIRE(device.texture_descriptions.size() == 1);

    output.resize(640, 360);
    update_sprite_output(device, nullptr, output);
    CHECK(output.texture != first_texture);
    CHECK(output.width == 640);
    CHECK(output.height == 360);
    REQUIRE(device.texture_descriptions.size() == 2);

    output.resize(0, 0);
    update_sprite_output(device, nullptr, output);
    CHECK_FALSE(output.texture);
    CHECK_FALSE(output.framebuffer);
    CHECK(output.width == 0);
    CHECK(output.height == 0);
}

TEST_CASE("Sprite phase appends indexed quads", "[sprite][geometry]") {
    SpritePhase phase;
    const Sprite sprite {};

    append_sprite_quad(phase, sprite, Transform2d {});
    append_sprite_quad(phase, sprite, Transform2d {.position = {2.0f, 0.0f}});

    REQUIRE(phase.vertices.size() == 8);
    REQUIRE(phase.indices.size() == 12);
    CHECK(phase.indices[6] == 4);
    CHECK(phase.indices[11] == 7);
}

TEST_CASE("Sprite visibility uses clip-space bounds", "[sprite][visibility]") {
    const auto clip_from_world = Matrix4x4::Identity;

    CHECK(is_sprite_quad_visible(
        make_sprite_quad(Sprite {}, Transform2d {}),
        clip_from_world
    ));
    CHECK(is_sprite_quad_visible(
        make_sprite_quad(Sprite {}, Transform2d {.position = {1.5f, 0.0f}}),
        clip_from_world
    ));
    CHECK_FALSE(is_sprite_quad_visible(
        make_sprite_quad(Sprite {}, Transform2d {.position = {2.0f, 0.0f}}),
        clip_from_world
    ));
    CHECK_FALSE(is_sprite_quad_visible(
        make_sprite_quad(Sprite {}, Transform2d {.position = {-2.0f, 0.0f}}),
        clip_from_world
    ));
    CHECK_FALSE(is_sprite_quad_visible(
        make_sprite_quad(Sprite {}, Transform2d {.position = {0.0f, 2.0f}}),
        clip_from_world
    ));
    CHECK_FALSE(is_sprite_quad_visible(
        make_sprite_quad(Sprite {}, Transform2d {.position = {0.0f, -2.0f}}),
        clip_from_world
    ));
    CHECK(is_sprite_quad_visible(
        make_sprite_quad(Sprite {.size = {6.0f, 6.0f}}, Transform2d {}),
        clip_from_world
    ));
}

TEST_CASE(
    "Sprite visibility supports rotated cameras and sprites",
    "[sprite][visibility]"
) {
    const Camera2d camera {.vertical_size = 4.0f};
    const Transform2d camera_transform {
        .position = {5.0f, -3.0f},
        .rotation = 45.0f,
    };
    const auto clip_from_world =
        camera_2d_clip_from_world(camera, camera_transform, 200, 200);

    CHECK(is_sprite_quad_visible(
        make_sprite_quad(
            Sprite {},
            Transform2d {.position = {5.0f, -3.0f}, .rotation = 30.0f}
        ),
        clip_from_world
    ));
    CHECK_FALSE(is_sprite_quad_visible(
        make_sprite_quad(
            Sprite {},
            Transform2d {.position = {15.0f, -3.0f}, .rotation = 30.0f}
        ),
        clip_from_world
    ));
}

TEST_CASE("Sprite quad applies atlas UVs and flips", "[sprite][geometry]") {
    Sprite sprite {
        .uv_rect = {
            .min = {0.25f, 0.125f},
            .max = {0.75f, 0.875f},
        },
    };

    const auto unflipped = make_sprite_quad(sprite, Transform2d {});
    CHECK(unflipped.vertices[0].uv == Vector2 {0.25f, 0.125f});
    CHECK(unflipped.vertices[1].uv == Vector2 {0.75f, 0.125f});
    CHECK(unflipped.vertices[2].uv == Vector2 {0.25f, 0.875f});
    CHECK(unflipped.vertices[3].uv == Vector2 {0.75f, 0.875f});

    sprite.flip_x = true;
    const auto flipped_x = make_sprite_quad(sprite, Transform2d {});
    CHECK(flipped_x.vertices[0].uv == Vector2 {0.75f, 0.125f});
    CHECK(flipped_x.vertices[1].uv == Vector2 {0.25f, 0.125f});

    sprite.flip_x = false;
    sprite.flip_y = true;
    const auto flipped_y = make_sprite_quad(sprite, Transform2d {});
    CHECK(flipped_y.vertices[0].uv == Vector2 {0.25f, 0.875f});
    CHECK(flipped_y.vertices[2].uv == Vector2 {0.25f, 0.125f});
}

TEST_CASE(
    "Sprite phase batches adjacent atlas regions sharing one texture",
    "[sprite][batch]"
) {
    SpritePhase phase;
    auto layout =
        std::make_shared<ResourceLayout>(ResourceLayoutDescription {});
    auto first_texture = std::make_shared<ResourceSet>(
        ResourceSetDescription {.layout = layout, .resources = {}}
    );
    auto second_texture = std::make_shared<ResourceSet>(
        ResourceSetDescription {.layout = layout, .resources = {}}
    );

    append_sprite(
        phase,
        Sprite {
            .uv_rect = {.min = {0.0f, 0.0f}, .max = {0.5f, 1.0f}},
        },
        Transform2d {},
        first_texture
    );
    append_sprite(
        phase,
        Sprite {
            .uv_rect = {.min = {0.5f, 0.0f}, .max = {1.0f, 1.0f}},
        },
        Transform2d {},
        first_texture
    );
    append_sprite(phase, Sprite {}, Transform2d {}, second_texture);

    REQUIRE(phase.batches.size() == 2);
    CHECK(phase.batches[0].texture_set == first_texture);
    CHECK(phase.batches[0].first_index == 0);
    CHECK(phase.batches[0].index_count == 12);
    CHECK(phase.batches[1].texture_set == second_texture);
    CHECK(phase.batches[1].first_index == 12);
    CHECK(phase.batches[1].index_count == 6);
    CHECK(phase.vertices[4].uv == Vector2 {0.5f, 0.0f});
}
