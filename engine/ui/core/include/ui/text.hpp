#pragma once

#include "math/vector.hpp"
#include "text/text.hpp"

namespace fei::ui {

using Text = text::Text;

struct TextNodeFlags {
    bool needs_measure {true};
    bool needs_layout {true};
    bool has_layout_size {false};
    Vector2 layout_size;

    bool operator==(const TextNodeFlags&) const = default;
};

} // namespace fei::ui
