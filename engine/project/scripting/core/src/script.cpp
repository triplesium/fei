#include "project_scripting/script.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace fei::project_scripting {

bool script_path_has_extension(
    const AssetReference& reference,
    std::string_view extension
) {
    auto actual = reference.fallback_path.path().extension().generic_string();
    std::ranges::transform(actual, actual.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    std::string expected(extension);
    std::ranges::transform(
        expected,
        expected.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return actual == expected;
}

} // namespace fei::project_scripting
