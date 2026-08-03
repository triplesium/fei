#pragma once
#include "base/hash.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fei {

using ShaderDefValue = std::variant<bool, std::int32_t, std::uint32_t>;

struct ShaderDefVal {
    std::string name;
    ShaderDefValue value {true};

    static ShaderDefVal bool_def(std::string name, bool value = true) {
        return ShaderDefVal {
            .name = std::move(name),
            .value = value,
        };
    }

    static ShaderDefVal int_def(std::string name, std::int32_t value) {
        return ShaderDefVal {
            .name = std::move(name),
            .value = value,
        };
    }

    static ShaderDefVal uint_def(std::string name, std::uint32_t value) {
        return ShaderDefVal {
            .name = std::move(name),
            .value = value,
        };
    }

    bool operator==(const ShaderDefVal&) const = default;
};

using ShaderDefs = std::vector<ShaderDefVal>;

inline bool
shader_def_value_less(const ShaderDefValue& lhs, const ShaderDefValue& rhs) {
    if (lhs.index() != rhs.index()) {
        return lhs.index() < rhs.index();
    }

    switch (lhs.index()) {
        case 0:
            return std::get<bool>(lhs) < std::get<bool>(rhs);
        case 1:
            return std::get<std::int32_t>(lhs) < std::get<std::int32_t>(rhs);
        case 2:
            return std::get<std::uint32_t>(lhs) < std::get<std::uint32_t>(rhs);
        default:
            return false;
    }
}

inline bool shader_def_less(const ShaderDefVal& lhs, const ShaderDefVal& rhs) {
    if (lhs.name != rhs.name) {
        return lhs.name < rhs.name;
    }
    return shader_def_value_less(lhs.value, rhs.value);
}

inline ShaderDefs normalized_shader_defs(ShaderDefs defs) {
    std::sort(defs.begin(), defs.end(), shader_def_less);
    defs.erase(std::unique(defs.begin(), defs.end()), defs.end());
    return defs;
}

} // namespace fei

namespace std {
template<>
struct hash<fei::ShaderDefVal> { // NOLINT(readability-identifier-naming)
    std::size_t operator()(const fei::ShaderDefVal& def) const {
        std::size_t seed = 0;
        fei::hash_combine(seed, def.name);
        fei::hash_combine(seed, def.value.index());
        std::visit(
            [&](const auto& value) {
                fei::hash_combine(seed, value);
            },
            def.value
        );
        return seed;
    }
};
} // namespace std
