#include "ui_rendering/renderer.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ets;

TEST_CASE("UI background creates a screen-space quad", "[ui][rendering]") {
    const ui::ComputedNode node {
        .position = {10.0f, 20.0f},
        .size = {100.0f, 50.0f},
        .content_size = {100.0f, 50.0f},
    };
    const ui::BackgroundColor background {
        .color = {0.25f, 0.5f, 0.75f, 1.0f},
    };

    const auto quad = ui::rendering::make_quad(node, background);

    REQUIRE(quad);
    CHECK(quad->vertices[0].position == Vector2 {10.0f, 20.0f});
    CHECK(quad->vertices[1].position == Vector2 {110.0f, 20.0f});
    CHECK(quad->vertices[2].position == Vector2 {10.0f, 70.0f});
    CHECK(quad->vertices[3].position == Vector2 {110.0f, 70.0f});
    CHECK(quad->vertices[0].color.a == 1.0f);
    CHECK(quad->indices == std::array<std::uint32_t, 6> {0, 1, 2, 2, 1, 3});
}

TEST_CASE("UI phase appends quads into one indexed stream", "[ui][rendering]") {
    ui::rendering::Phase phase;
    const ui::BackgroundColor background {
        .color = {1.0f, 1.0f, 1.0f, 1.0f},
    };
    phase.append(*ui::rendering::make_quad(
        ui::ComputedNode {.size = {10.0f, 10.0f}},
        background
    ));
    phase.append(*ui::rendering::make_quad(
        ui::ComputedNode {
            .position = {20.0f, 0.0f},
            .size = {10.0f, 10.0f},
        },
        background
    ));

    REQUIRE(phase.vertices.size() == 8);
    REQUIRE(phase.indices.size() == 12);
    CHECK(phase.indices[6] == 4);
    CHECK(phase.indices[11] == 7);

    phase.clear();
    CHECK(phase.vertices.empty());
    CHECK(phase.indices.empty());
    CHECK(phase.glyph_count == 0);
    CHECK(phase.glyph_batch_count == 0);
    CHECK_FALSE(phase.active);
}

TEST_CASE("UI phase tracks queued glyph batches", "[ui][rendering][text]") {
    ui::rendering::Phase phase;
    const auto quad = ui::rendering::make_glyph_quad(
        ui::ComputedNode {.size = {20.0f, 20.0f}},
        text::PositionedGlyph {
            .size = {8.0f, 12.0f},
            .uv = {.min = {0.0f, 0.0f}, .max = {0.5f, 0.5f}},
        },
        Color4F {1.0f, 1.0f, 1.0f, 1.0f}
    );
    REQUIRE(quad);

    phase.append_glyph(*quad);
    phase.append_glyph(*quad);

    CHECK(phase.glyph_count == 2);
    CHECK(phase.glyph_batch_count == 1);
}

TEST_CASE("UI quad is clipped on the CPU", "[ui][rendering]") {
    const ui::ComputedNode node {
        .position = {10.0f, 20.0f},
        .size = {100.0f, 50.0f},
    };
    const ui::BackgroundColor background {.color = {1.0f, 1.0f, 1.0f, 1.0f}};

    const auto quad = ui::rendering::make_quad(
        node,
        background,
        Rect {.min = {30.0f, 0.0f}, .max = {90.0f, 60.0f}}
    );

    REQUIRE(quad);
    CHECK(quad->vertices[0].position == Vector2 {30.0f, 20.0f});
    CHECK(quad->vertices[3].position == Vector2 {90.0f, 60.0f});
    CHECK_FALSE(
        ui::rendering::make_quad(
            node,
            background,
            Rect {.min = {200.0f, 200.0f}, .max = {300.0f, 300.0f}}
        )
    );
}

TEST_CASE("UI image auto-fits and clipping preserves UVs", "[ui][rendering]") {
    const ui::ComputedNode node {.size = {200.0f, 200.0f}};
    const ui::ImageNode image {};

    const auto fitted =
        ui::rendering::make_image_quad(node, image, Vector2 {100.0f, 50.0f});
    REQUIRE(fitted);
    CHECK(fitted->vertices[0].position == Vector2 {0.0f, 50.0f});
    CHECK(fitted->vertices[3].position == Vector2 {200.0f, 150.0f});

    auto sliced = image;
    sliced.mode = ui::NodeImageMode::Stretch;
    sliced.source_rect = Rect {.min = {50.0f, 0.0f}, .max = {150.0f, 100.0f}};
    const auto clipped = ui::rendering::make_image_quad(
        ui::ComputedNode {.size = {200.0f, 100.0f}},
        sliced,
        Vector2 {200.0f, 100.0f},
        Rect {.min = {50.0f, 0.0f}, .max = {150.0f, 100.0f}}
    );
    REQUIRE(clipped);
    CHECK(clipped->vertices[0].uv == Vector2 {0.375f, 1.0f});
    CHECK(clipped->vertices[3].uv == Vector2 {0.625f, 0.0f});

    auto partial = image;
    partial.source_rect = Rect {.min = {0.0f, 20.0f}, .max = {100.0f, 80.0f}};
    const auto partial_quad = ui::rendering::make_image_quad(
        ui::ComputedNode {.size = {100.0f, 60.0f}},
        partial,
        Vector2 {100.0f, 200.0f}
    );
    REQUIRE(partial_quad);
    CHECK(partial_quad->vertices[0].uv == Vector2 {0.0f, 0.9f});
    CHECK(partial_quad->vertices[3].uv == Vector2 {1.0f, 0.6f});

    partial.flip_y = true;
    const auto flipped = ui::rendering::make_image_quad(
        ui::ComputedNode {.size = {100.0f, 60.0f}},
        partial,
        Vector2 {100.0f, 200.0f}
    );
    REQUIRE(flipped);
    CHECK(flipped->vertices[0].uv == Vector2 {0.0f, 0.6f});
    CHECK(flipped->vertices[3].uv == Vector2 {1.0f, 0.9f});
}

TEST_CASE("UI border quad carries resolved edge style", "[ui][rendering]") {
    const ui::ComputedNode node {
        .size = {100.0f, 60.0f},
        .border = {.left = 5.0f},
        .border_radius = {.top_left = 12.0f},
    };
    const auto quad = ui::rendering::make_border_quad(
        node,
        Color4F {1.0f, 0.0f, 0.0f, 1.0f},
        ui::rendering::BorderSide::Left
    );

    REQUIRE(quad);
    CHECK(quad->vertices[0].kind == 1.0f);
    CHECK(quad->vertices[0].border_side == 1.0f);
    CHECK(quad->vertices[0].border.x == 5.0f);
    CHECK(quad->vertices[0].border_radius.x == 12.0f);
    CHECK_FALSE(
        ui::rendering::make_border_quad(
            node,
            Color4F {1.0f, 1.0f, 1.0f, 1.0f},
            ui::rendering::BorderSide::Right
        )
    );
}

TEST_CASE("UI glyph quad uses the text content origin", "[ui][rendering]") {
    const ui::ComputedNode node {
        .size = {100.0f, 60.0f},
        .content_size = {80.0f, 40.0f},
        .content_position = {12.0f, 18.0f},
    };
    const text::PositionedGlyph glyph {
        .position = {3.0f, 4.0f},
        .size = {10.0f, 12.0f},
        .uv = {.min = {0.25f, 0.5f}, .max = {0.5f, 0.75f}},
    };
    const auto quad = ui::rendering::make_glyph_quad(
        node,
        glyph,
        Color4F {1.0f, 1.0f, 1.0f, 1.0f}
    );

    REQUIRE(quad);
    CHECK(quad->vertices[0].position == Vector2 {15.0f, 22.0f});
    CHECK(quad->vertices[3].position == Vector2 {25.0f, 34.0f});
    CHECK(quad->vertices[0].uv == Vector2 {0.25f, 0.5f});
    CHECK(quad->vertices[0].kind == 2.0f);
}
