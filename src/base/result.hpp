#pragma once

#include <expected>
#include <type_traits>
#include <utility>

namespace fei {

template<class T, class E>
using Result = std::expected<T, E>;

template<class E>
using Status = Result<void, E>;

template<class Error>
constexpr std::unexpected<std::remove_cvref_t<Error>>
failure(Error&& error) {
    using ErrorType = std::remove_cvref_t<Error>;
    return std::unexpected<ErrorType>(std::forward<Error>(error));
}

} // namespace fei
