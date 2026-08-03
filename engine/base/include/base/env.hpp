#pragma once

#include <charconv>
#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace fei {

std::optional<std::string> read_environment_variable(std::string_view name);

template<typename T>
concept EnvironmentNumber =
    (std::integral<T> && !std::same_as<std::remove_cv_t<T>, bool>) ||
    std::floating_point<T>;

template<EnvironmentNumber T>
std::optional<T> read_environment_variable(std::string_view name) {
    auto value = read_environment_variable(name);
    if (!value) {
        return std::nullopt;
    }

    T result {};
    auto text = std::string_view(*value);
    auto [ptr, ec] =
        std::from_chars(text.data(), text.data() + text.size(), result);
    if (ec != std::errc {} || ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return result;
}

} // namespace fei
