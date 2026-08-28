#pragma once

#include "math/vector.hpp"
#include "text/text.hpp"

namespace ets::ui {

using Text = text::Text;

struct ComputedTextBlock {
    text::TextMeasureInfo measure;
    bool has_measure {false};

    bool operator==(const ComputedTextBlock&) const = default;
};

struct TextNodeFlags {
    bool needs_measure {true};
    bool needs_layout {true};
    bool has_layout_size {false};
    Vector2 layout_size;

    bool operator==(const TextNodeFlags&) const = default;
};

} // namespace ets::ui
