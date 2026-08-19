#pragma once

#include "asset/handle.hpp"
#include "base/optional.hpp"
#include "core/image.hpp"
#include "math/color.hpp"
#include "math/primitives.hpp"
#include "math/vector.hpp"
#include "refl/reflect.hpp"
#include "text/font.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace fei::text {

FEI_REFLECT(Component)
struct Text {
    std::string value;
};

FEI_REFLECT(Component)
struct TextFont {
    Handle<Font> font;
    float font_size {16.0f};
};

FEI_REFLECT(Component)
struct TextColor {
    Color4F color {1.0f, 1.0f, 1.0f, 1.0f};
};

enum class LineBreak {
    WordBoundary,
    AnyCharacter,
    WordOrCharacter,
    NoWrap,
};

enum class Justify {
    Left,
    Center,
    Right,
};

FEI_REFLECT(Component)
struct TextLayout {
    Justify justify {Justify::Left};
    LineBreak line_break {LineBreak::WordOrCharacter};

    bool operator==(const TextLayout&) const = default;
};

struct MeasuredGlyph {
    char32_t codepoint {0};
    int32 glyph_id {0};
    float advance {0.0f};
    float kerning {0.0f};
    bool whitespace {false};

    bool operator==(const MeasuredGlyph&) const = default;
};

struct TextLine {
    std::size_t begin {0};
    std::size_t end {0};
    float width {0.0f};

    bool operator==(const TextLine&) const = default;
};

struct TextMeasureInfo {
    Vector2 min;
    Vector2 max;
    float ascent {0.0f};
    float line_height {0.0f};
    std::vector<MeasuredGlyph> glyphs;

    bool operator==(const TextMeasureInfo&) const = default;

    [[nodiscard]] std::vector<TextLine>
    lines(Optional<float> max_width, LineBreak line_break) const;
    [[nodiscard]] Vector2
    compute_size(Optional<float> max_width, LineBreak line_break) const;
};

struct PositionedGlyph {
    Vector2 position;
    Vector2 size;
    Rect uv;
    Handle<Image> atlas;

    bool operator==(const PositionedGlyph& other) const {
        return position == other.position && size == other.size &&
               uv.min == other.uv.min && uv.max == other.uv.max &&
               atlas.id() == other.atlas.id();
    }
};

FEI_REFLECT(Component)
struct TextLayoutInfo {
    Vector2 size;
    std::vector<PositionedGlyph> glyphs;

    bool operator==(const TextLayoutInfo&) const = default;
};

} // namespace fei::text
