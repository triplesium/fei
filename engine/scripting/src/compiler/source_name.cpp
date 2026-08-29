#include "source_name.hpp"

#include <algorithm>

namespace ets::detail::luau_compiler {

std::string module_name_from_source(std::string_view source_name) {
    while (source_name.starts_with("./") || source_name.starts_with(".\\")) {
        source_name.remove_prefix(2);
    }

    std::string result {source_name};
    if (const auto source_delimiter = result.find("://");
        source_delimiter != std::string::npos) {
        result.replace(source_delimiter, 3, ".");
    }
    const auto separator = result.find_last_of("/\\");
    const auto extension = result.find_last_of('.');
    if (extension != std::string::npos &&
        (separator == std::string::npos || extension > separator)) {
        result.erase(extension);
    }
    std::ranges::replace(result, '/', '.');
    std::ranges::replace(result, '\\', '.');
    return result.empty() ? std::string {"script"} : result;
}

} // namespace ets::detail::luau_compiler
