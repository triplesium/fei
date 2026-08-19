#include "ui/measurement.hpp"

#include <type_traits>

namespace fei::ui {

ContentSize ContentSize::fixed(Vector2 size) {
    return {.measure = FixedMeasure {.size = size}};
}

Vector2 ContentSize::compute(const MeasureArgs& args) const {
    auto measured = std::visit(
        [&](const auto& value) -> Vector2 {
            using Measure = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Measure, std::monostate>) {
                return Vector2::Zero;
            } else if constexpr (std::is_same_v<Measure, FixedMeasure>) {
                return value.size;
            } else {
                Optional<float> width = args.known_width;
                if (!width &&
                    args.available_width.kind == AvailableSpaceKind::Definite) {
                    width = args.available_width.value;
                }
                if (!width && args.available_width.kind ==
                                  AvailableSpaceKind::MinContent) {
                    width = value.info.min.x;
                }
                return value.info.compute_size(width, value.layout.line_break);
            }
        },
        measure
    );
    if (args.known_width) {
        measured.x = *args.known_width;
    }
    if (args.known_height) {
        measured.y = *args.known_height;
    }
    return measured;
}

} // namespace fei::ui
