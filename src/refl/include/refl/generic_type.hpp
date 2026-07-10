#pragma once

#include "base/result.hpp"
#include "refl/type.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace fei {

// Stores reflected template arguments, including non-type arguments such as
// std::array<T, N>'s N. GenericType::argument_type_ids is kept as the compact
// type-only view, while GenericType::arguments preserves the full signature.
struct GenericArgument {
    enum class Kind {
        Type,
        SignedInteger,
        UnsignedInteger,
    };

    Kind kind {Kind::Type};
    TypeId type_id;
    std::int64_t signed_integer {0};
    std::uint64_t unsigned_integer {0};

    static GenericArgument type(TypeId id) {
        return GenericArgument {
            .kind = Kind::Type,
            .type_id = id,
        };
    }

    static GenericArgument signed_value(std::int64_t value) {
        return GenericArgument {
            .kind = Kind::SignedInteger,
            .signed_integer = value,
        };
    }

    static GenericArgument unsigned_value(std::uint64_t value) {
        return GenericArgument {
            .kind = Kind::UnsignedInteger,
            .unsigned_integer = value,
        };
    }

    bool operator==(const GenericArgument&) const = default;
};

inline std::vector<GenericArgument>
generic_arguments_from_type_ids(const std::vector<TypeId>& type_ids) {
    std::vector<GenericArgument> arguments;
    arguments.reserve(type_ids.size());
    for (auto type_id : type_ids) {
        arguments.push_back(GenericArgument::type(type_id));
    }
    return arguments;
}

// Describes one concrete specialization, for example std::vector<int>.
struct GenericType {
    TypeId specialized_type_id;
    TypeId generic_type_id;
    std::string generic_name;
    std::vector<TypeId> argument_type_ids;
    std::vector<GenericArgument> arguments;
};

// Specialize GenericTypeInfo to opt a template into automatic generic
// reflection. register_type<T>() uses supported as the compile-time gate,
// recursively registers Dependencies, and calls make_container_adapter() when
// the specialization exposes container operations.
template<class T, class = void>
struct GenericTypeInfo {
    static constexpr bool supported = false;
    using Dependencies = std::tuple<>;
};

template<class T, class E>
struct GenericTypeInfo<Result<T, E>> {
    static constexpr bool supported = true;
    using Dependencies = std::tuple<T, E>;

    static TypeId generic_type_id() {
        return TypeId(std::string_view {"fei::Result"});
    }

    static std::string generic_name() { return "fei::Result"; }

    static std::vector<TypeId> argument_type_ids() {
        return {type_id<T>(), type_id<E>()};
    }
};

} // namespace fei
