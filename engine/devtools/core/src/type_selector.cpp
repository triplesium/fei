#include "devtools/type_selector.hpp"

#include "refl/registry.hpp"

#include <charconv>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace fei::devtools {

Result<Type&, TypeSelectorError>
resolve_type_selector(std::string_view selector) {
    if (selector.empty()) {
        return failure(
            TypeSelectorError {400, "Type selector must not be empty"}
        );
    }

    auto& registry = Registry::instance();
    if (selector.starts_with("0x") || selector.starts_with("0X")) {
        std::uint64_t id {};
        auto digits = selector.substr(2);
        auto [end, error] = std::from_chars(
            digits.data(),
            digits.data() + digits.size(),
            id,
            16
        );
        if (digits.empty() || error != std::errc {} ||
            end != digits.data() + digits.size()) {
            return failure(
                TypeSelectorError {400, "Invalid hexadecimal type id"}
            );
        }
        auto type = registry.try_get_type(TypeId {id});
        if (!type) {
            return failure(TypeSelectorError {404, type.error().message});
        }
        return *type;
    }

    auto type = registry.try_get_type(selector);
    if (!type) {
        auto status =
            type.error().kind == RegistryError::Kind::AmbiguousTypeName ? 409 :
                                                                          404;
        return failure(TypeSelectorError {status, type.error().message});
    }
    return *type;
}

std::string format_type_id(TypeId id) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setfill('0') << std::setw(16) << id.id();
    return stream.str();
}

} // namespace fei::devtools
