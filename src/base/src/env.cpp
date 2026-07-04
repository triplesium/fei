#include "base/env.hpp"

#include <cstdlib>

namespace fei {

std::optional<std::string> read_environment_variable(std::string_view name) {
    std::string key(name);
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, key.c_str()) != 0 || value == nullptr) {
        return std::nullopt;
    }

    std::string result(value, size > 0 ? size - 1 : 0);
    std::free(value);
    return result;
#else
    auto* value = std::getenv(key.c_str());
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

} // namespace fei
