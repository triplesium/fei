#pragma once

#include "base/optional.hpp"
#include "math/vector.hpp"
#include "refl/reflect.hpp"
#include "text/text.hpp"

#include <variant>

namespace ets::ui {

enum class AvailableSpaceKind {
    Definite,
    MinContent,
    MaxContent,
};

struct AvailableSpace {
    AvailableSpaceKind kind {AvailableSpaceKind::MaxContent};
    float value {0.0f};

    [[nodiscard]] static constexpr AvailableSpace definite(float value) {
        return {.kind = AvailableSpaceKind::Definite, .value = value};
    }
    [[nodiscard]] static constexpr AvailableSpace min_content() {
        return {.kind = AvailableSpaceKind::MinContent};
    }
    [[nodiscard]] static constexpr AvailableSpace max_content() {
        return {.kind = AvailableSpaceKind::MaxContent};
    }
};

struct MeasureArgs {
    Optional<float> known_width;
    Optional<float> known_height;
    AvailableSpace available_width;
    AvailableSpace available_height;
};

struct FixedMeasure {
    Vector2 size;

    bool operator==(const FixedMeasure&) const = default;
};

struct TextMeasure {
    text::TextMeasureInfo info;
    text::TextLayout layout;

    bool operator==(const TextMeasure&) const = default;
};

using NodeMeasure = std::variant<std::monostate, FixedMeasure, TextMeasure>;

ETS_REFLECT(Component)
struct ContentSize {
    NodeMeasure measure;

    [[nodiscard]] static ContentSize fixed(Vector2 size);
    [[nodiscard]] Vector2 compute(const MeasureArgs& args) const;

    bool operator==(const ContentSize&) const = default;
};

} // namespace ets::ui
