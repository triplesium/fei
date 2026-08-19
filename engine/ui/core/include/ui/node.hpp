#pragma once

#include "base/types.hpp"
#include "ecs/fwd.hpp"
#include "math/color.hpp"
#include "math/primitives.hpp"
#include "math/vector.hpp"
#include "refl/reflect.hpp"

#include <vector>

namespace fei::ui {

FEI_REFLECT()
enum class LengthUnit {
    Auto,
    Px,
    Percent,
};

FEI_REFLECT()
struct Length {
    float value {0.0f};
    LengthUnit unit {LengthUnit::Auto};

    [[nodiscard]] static constexpr Length automatic() { return {}; }
    [[nodiscard]] static constexpr Length px(float value) {
        return {.value = value, .unit = LengthUnit::Px};
    }
    [[nodiscard]] static constexpr Length percent(float value) {
        return {.value = value, .unit = LengthUnit::Percent};
    }

    [[nodiscard]] constexpr bool is_auto() const {
        return unit == LengthUnit::Auto;
    }

    bool operator==(const Length&) const = default;
};

[[nodiscard]] constexpr Length px(float value) {
    return Length::px(value);
}
[[nodiscard]] constexpr Length percent(float value) {
    return Length::percent(value);
}

FEI_REFLECT()
struct Edges {
    Length left;
    Length right;
    Length top;
    Length bottom;

    [[nodiscard]] static constexpr Edges all(Length value) {
        return {.left = value, .right = value, .top = value, .bottom = value};
    }

    [[nodiscard]] static constexpr Edges
    axes(Length horizontal, Length vertical) {
        return {
            .left = horizontal,
            .right = horizontal,
            .top = vertical,
            .bottom = vertical,
        };
    }

    bool operator==(const Edges&) const = default;
};

[[nodiscard]] constexpr Edges all(Length value) {
    return Edges::all(value);
}

[[nodiscard]] constexpr Edges axes(Length horizontal, Length vertical) {
    return Edges::axes(horizontal, vertical);
}

FEI_REFLECT()
enum class Display {
    Flex,
    None,
};

FEI_REFLECT()
enum class PositionType {
    Relative,
    Absolute,
};

FEI_REFLECT()
enum class OverflowAxis {
    Visible,
    Clip,
    Hidden,
    Scroll,
};

FEI_REFLECT()
struct Overflow {
    OverflowAxis x {OverflowAxis::Visible};
    OverflowAxis y {OverflowAxis::Visible};

    [[nodiscard]] static constexpr Overflow visible() { return {}; }
    [[nodiscard]] static constexpr Overflow clip() {
        return {.x = OverflowAxis::Clip, .y = OverflowAxis::Clip};
    }
    [[nodiscard]] static constexpr Overflow clip_x() {
        return {.x = OverflowAxis::Clip};
    }
    [[nodiscard]] static constexpr Overflow clip_y() {
        return {.y = OverflowAxis::Clip};
    }
    [[nodiscard]] static constexpr Overflow hidden() {
        return {.x = OverflowAxis::Hidden, .y = OverflowAxis::Hidden};
    }
    [[nodiscard]] static constexpr Overflow hidden_x() {
        return {.x = OverflowAxis::Hidden};
    }
    [[nodiscard]] static constexpr Overflow hidden_y() {
        return {.y = OverflowAxis::Hidden};
    }
    [[nodiscard]] static constexpr Overflow scroll() {
        return {.x = OverflowAxis::Scroll, .y = OverflowAxis::Scroll};
    }
    [[nodiscard]] static constexpr Overflow scroll_x() {
        return {.x = OverflowAxis::Scroll};
    }
    [[nodiscard]] static constexpr Overflow scroll_y() {
        return {.y = OverflowAxis::Scroll};
    }

    bool operator==(const Overflow&) const = default;
};

FEI_REFLECT(Component)
struct ScrollPosition {
    Vector2 offset;

    bool operator==(const ScrollPosition&) const = default;
};

FEI_REFLECT()
enum class FlexDirection {
    Row,
    Column,
};

FEI_REFLECT()
enum class AlignItems {
    Start,
    Center,
    End,
    Stretch,
};

FEI_REFLECT()
enum class JustifyContent {
    Start,
    Center,
    End,
    SpaceBetween,
};

FEI_REFLECT(Component)
struct Node {
    Display display {Display::Flex};
    PositionType position_type {PositionType::Relative};
    Overflow overflow;

    Length width;
    Length height;
    Length min_width;
    Length min_height;
    Length max_width;
    Length max_height;

    Edges margin;
    Edges border;
    Edges padding;

    FlexDirection flex_direction {FlexDirection::Column};
    AlignItems align_items {AlignItems::Stretch};
    JustifyContent justify_content {JustifyContent::Start};
    float flex_grow {0.0f};
    float flex_shrink {1.0f};
    Length flex_basis;
    Length gap {px(0.0f)};

    Length left;
    Length right;
    Length top;
    Length bottom;
};

FEI_REFLECT()
struct ResolvedBorder {
    float left {0.0f};
    float top {0.0f};
    float right {0.0f};
    float bottom {0.0f};

    bool operator==(const ResolvedBorder&) const = default;
};

FEI_REFLECT(Component)
struct BorderRadius {
    Length top_left;
    Length top_right;
    Length bottom_right;
    Length bottom_left;

    [[nodiscard]] static constexpr BorderRadius all(Length value) {
        return {
            .top_left = value,
            .top_right = value,
            .bottom_right = value,
            .bottom_left = value,
        };
    }
};

FEI_REFLECT()
struct ResolvedBorderRadius {
    float top_left {0.0f};
    float top_right {0.0f};
    float bottom_right {0.0f};
    float bottom_left {0.0f};

    bool operator==(const ResolvedBorderRadius&) const = default;
};

FEI_REFLECT(Component)
struct ComputedNode {
    // Position is the top-left corner in logical pixels relative to the UI
    // viewport. Size includes padding.
    Vector2 position;
    Vector2 size;
    Vector2 content_size;
    Vector2 content_position;
    Vector2 scroll_content_size;
    Vector2 scroll_position;
    ResolvedBorder border;
    ResolvedBorderRadius border_radius;

    bool operator==(const ComputedNode&) const = default;
};

FEI_REFLECT(Component)
struct CalculatedClip {
    // The inherited clip rectangle in viewport logical pixels. A node's own
    // overflow affects its descendants rather than the node itself.
    Rect clip;

    bool operator==(const CalculatedClip& other) const {
        return clip.min == other.clip.min && clip.max == other.clip.max;
    }
};

FEI_REFLECT(Component)
struct BackgroundColor {
    Color4F color {0.0f, 0.0f, 0.0f, 0.0f};
};

FEI_REFLECT(Component)
struct BorderColor {
    Color4F left;
    Color4F top;
    Color4F right;
    Color4F bottom;

    [[nodiscard]] static constexpr BorderColor all(Color4F color) {
        return {.left = color, .top = color, .right = color, .bottom = color};
    }
};

FEI_REFLECT(Component)
struct ZIndex {
    int32 value {0};
};

FEI_REFLECT(Component)
struct ComputedStackIndex {
    static constexpr uint32 HIDDEN = static_cast<uint32>(-1);

    uint32 value {HIDDEN};

    bool operator==(const ComputedStackIndex&) const = default;
};

struct Stack {
    // Back-to-front render order. The last entry receives interactions first.
    std::vector<Entity> nodes;
};

} // namespace fei::ui
