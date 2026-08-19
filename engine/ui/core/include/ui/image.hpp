#pragma once

#include "asset/handle.hpp"
#include "base/optional.hpp"
#include "core/image.hpp"
#include "math/color.hpp"
#include "math/primitives.hpp"
#include "math/vector.hpp"
#include "refl/reflect.hpp"

namespace fei::ui {

enum class NodeImageMode {
    Auto,
    Stretch,
};

FEI_REFLECT(Component)
struct ImageNode {
    Handle<Image> image;
    Color4F color {1.0f, 1.0f, 1.0f, 1.0f};
    NodeImageMode mode {NodeImageMode::Auto};
    Optional<Rect> source_rect;
    bool flip_x {false};
    bool flip_y {false};
};

FEI_REFLECT(Component)
struct ImageNodeSize {
    Vector2 size;

    bool operator==(const ImageNodeSize&) const = default;
};

} // namespace fei::ui
