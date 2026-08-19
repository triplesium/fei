#pragma once

#include "ecs/fwd.hpp"
#include "refl/reflect.hpp"

#include <cmath>
#include <cstdint>

namespace fei::ui_widgets {

FEI_REFLECT()
enum class SliderOrientation {
    Auto,
    Horizontal,
    Vertical,
};

FEI_REFLECT()
enum class TrackClick {
    Drag,
    Step,
    Snap,
};

FEI_REFLECT(Component)
struct Slider {
    TrackClick track_click {TrackClick::Drag};
    SliderOrientation orientation {SliderOrientation::Auto};
};

FEI_REFLECT(Component)
struct SliderThumb {};

FEI_REFLECT(Component)
struct SliderValue {
    float value {0.0f};

    bool operator==(const SliderValue&) const = default;
};

FEI_REFLECT(Component)
struct SliderRange {
    float minimum {0.0f};
    float maximum {1.0f};

    [[nodiscard]] static SliderRange new_range(float start, float end) {
        return {.minimum = start, .maximum = end};
    }
    [[nodiscard]] float start() const { return minimum; }
    [[nodiscard]] float end() const { return maximum; }
    [[nodiscard]] float span() const { return maximum - minimum; }
    [[nodiscard]] float center() const { return (minimum + maximum) * 0.5f; }
    [[nodiscard]] float clamp(float value) const;
    [[nodiscard]] float thumb_position(float value) const;

    bool operator==(const SliderRange&) const = default;
};

FEI_REFLECT(Component)
struct SliderStep {
    float value {1.0f};

    bool operator==(const SliderStep&) const = default;
};

FEI_REFLECT(Component)
struct SliderPrecision {
    std::int32_t decimal_places {0};

    [[nodiscard]] float round(float value) const {
        const auto factor = std::pow(10.0f, static_cast<float>(decimal_places));
        return std::round(value * factor) / factor;
    }

    bool operator==(const SliderPrecision&) const = default;
};

FEI_REFLECT(Component)
struct SliderDragState {
    bool dragging {false};
    bool changed {false};
    float offset {0.0f};
    float pointer_start {0.0f};
    float pointer_position {0.0f};
};

FEI_REFLECT()
enum class SliderValueChangeKind {
    Absolute,
    Relative,
    RelativeStep,
};

FEI_REFLECT()
struct SliderValueChange {
    SliderValueChangeKind kind {SliderValueChangeKind::Absolute};
    float value {0.0f};

    [[nodiscard]] static SliderValueChange absolute(float value) {
        return {.kind = SliderValueChangeKind::Absolute, .value = value};
    }
    [[nodiscard]] static SliderValueChange relative(float value) {
        return {.kind = SliderValueChangeKind::Relative, .value = value};
    }
    [[nodiscard]] static SliderValueChange relative_step(float value) {
        return {.kind = SliderValueChangeKind::RelativeStep, .value = value};
    }

    bool operator==(const SliderValueChange&) const = default;
};

FEI_REFLECT()
struct SetSliderValue {
    Entity entity;
    SliderValueChange change;

    bool operator==(const SetSliderValue&) const = default;
};

} // namespace fei::ui_widgets
