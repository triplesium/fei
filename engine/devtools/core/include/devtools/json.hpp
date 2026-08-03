#pragma once

#include "base/result.hpp"
#include "refl/ref.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fei::devtools {

Result<std::string, std::string> encode_json(Ref value);

Result<Val, std::string> decode_json(TypeId type_id, std::string_view text);

template<typename T>
Result<std::remove_cvref_t<T>, std::string> decode_json(std::string_view text) {
    using U = std::remove_cvref_t<T>;
    auto value = decode_json(type_id<U>(), text);
    if (!value) {
        return failure(std::move(value.error()));
    }
    return std::move(value->template get<U>());
}

} // namespace fei::devtools
