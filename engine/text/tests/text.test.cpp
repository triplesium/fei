#include "app/app.hpp"
#include "asset/assets.hpp"
#include "asset/embed.hpp"
#include "core/image.hpp"
#include "ecs/world.hpp"
#include "text/editable.hpp"
#include "text/font.hpp"
#include "text/pipeline.hpp"
#include "text/plugin.hpp"

#include <catch2/catch_test_macros.hpp>

EMBED(Cousine_Regular_ttf, "text-test-font.ttf");

using namespace fei;

TEST_CASE("Font reads metrics and rasterizes glyphs", "[text][font]") {
    auto reader = EmbeddedAssets::get("text-test-font.ttf").reader();
    auto font = text::Font::from_bytes(std::span(reader.data(), reader.size()));
    REQUIRE(font);

    const auto metrics = (*font)->metrics(32.0f);
    CHECK(metrics.ascent > 0.0f);
    CHECK(metrics.descent < 0.0f);
    const auto glyph_id = (*font)->glyph_id(U'A');
    CHECK(glyph_id > 0);
    const auto glyph = (*font)->rasterize(glyph_id, 32.0f);
    CHECK(glyph.width > 0);
    CHECK(glyph.height > 0);
    CHECK(glyph.pixels.size() == glyph.width * glyph.height);
}

TEST_CASE("Text pipeline measures and caches glyph atlas entries", "[text]") {
    App app;
    app.add_plugin<text::TextPlugin>().add_plugin<ImagePlugin>();
    app.finish();

    auto reader = EmbeddedAssets::get("text-test-font.ttf").reader();
    auto font = text::Font::from_bytes(std::span(reader.data(), reader.size()));
    REQUIRE(font);
    const auto font_handle =
        app.resource<Assets<text::Font>>().add(std::move(*font));
    const auto loaded_font =
        app.resource<Assets<text::Font>>().get(font_handle);
    REQUIRE(loaded_font);

    auto& pipeline = app.resource<text::TextPipeline>();
    const auto measure = pipeline.create_measure(*loaded_font, "Hello", 32.0f);
    const auto measured = measure.max;
    CHECK(measured.x > 0.0f);
    CHECK(measured.y > 0.0f);

    text::TextLayoutInfo layout;
    pipeline.layout(
        font_handle.id(),
        *loaded_font,
        measure,
        32.0f,
        text::TextLayout {},
        measured,
        app.resource<Assets<Image>>(),
        layout
    );
    CHECK(layout.size == measured);
    REQUIRE(layout.glyphs.size() == 5);
    CHECK(layout.glyphs.front().atlas);
    CHECK(app.resource<Assets<Image>>().get(layout.glyphs.front().atlas));
}

TEST_CASE(
    "Text measure responds to Bevy-style line break constraints",
    "[text]"
) {
    auto reader = EmbeddedAssets::get("text-test-font.ttf").reader();
    auto font = text::Font::from_bytes(std::span(reader.data(), reader.size()));
    REQUIRE(font);

    text::TextPipeline pipeline;
    const auto measure =
        pipeline.create_measure(**font, "wrap these words", 24.0f);
    const auto no_wrap =
        measure.compute_size(measure.max.x * 0.4f, text::LineBreak::NoWrap);
    const auto wrapped = measure.compute_size(
        measure.max.x * 0.4f,
        text::LineBreak::WordOrCharacter
    );

    CHECK(no_wrap.y == measure.line_height);
    CHECK(wrapped.y > no_wrap.y);
    CHECK(measure.min.x <= measure.max.x);
}

TEST_CASE("EditableText applies queued UTF-8 edits", "[text][editable]") {
    World world;
    world.add_resource(Events<text::TextChanged> {});
    const auto entity = world.entity();
    world.add_component(entity, text::Text {.value = "A\xe7\x95\x8c"});
    world.add_component(entity, text::EditableText {.anchor = 4, .cursor = 4});
    auto editable = world.get_component_rw<text::EditableText>(entity);
    editable->queue(text::TextEdit::move(text::TextEditKind::Backspace));
    editable->queue(text::TextEdit::insert("B"));

    world.run_system_once(text::apply_text_edits);

    CHECK(world.get_component<text::Text>(entity).value == "AB");
    const auto& result = world.get_component<text::EditableText>(entity);
    CHECK(result.cursor == 2);
    CHECK_FALSE(result.has_selection());
    const auto changed =
        world.resource<Events<text::TextChanged>>().get_event(0);
    REQUIRE(changed);
    CHECK(changed->event.entity == entity);
    CHECK(changed->event.value == "AB");
}

TEST_CASE(
    "EditableText keeps movement independent from value changes",
    "[text][editable]"
) {
    World world;
    world.add_resource(Events<text::TextChanged> {});
    const auto entity = world.entity();
    world.add_component(entity, text::Text {.value = "abcd"});
    world.add_component(entity, text::EditableText {.anchor = 1, .cursor = 1});
    auto editable = world.get_component_rw<text::EditableText>(entity);
    editable->queue(text::TextEdit::move(text::TextEditKind::MoveRight, true));
    editable->queue(text::TextEdit::move(text::TextEditKind::MoveRight, true));

    world.run_system_once(text::apply_text_edits);

    const auto& result = world.get_component<text::EditableText>(entity);
    CHECK(result.selection_start() == 1);
    CHECK(result.selection_end() == 3);
    CHECK(world.resource<Events<text::TextChanged>>().size() == 0);
}
