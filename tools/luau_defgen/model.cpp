#include "model.hpp"

#include <algorithm>

namespace ets::luau_defgen {

bool has_annotation(
    const std::vector<Annotation>& annotations,
    const std::string_view name
) {
    return std::ranges::any_of(
        annotations,
        [name](const Annotation& annotation) {
            return annotation.name == name;
        }
    );
}

} // namespace ets::luau_defgen
