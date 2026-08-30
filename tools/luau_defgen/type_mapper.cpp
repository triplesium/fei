#include "type_mapper.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <regex>
#include <unordered_set>

namespace ets::luau_defgen {
namespace {

[[nodiscard]] std::string trim(std::string value) {
    const auto not_space = [](const unsigned char character) {
        return std::isspace(character) == 0;
    };
    value.erase(
        value.begin(),
        std::find_if(value.begin(), value.end(), not_space)
    );
    value.erase(
        std::find_if(value.rbegin(), value.rend(), not_space).base(),
        value.end()
    );
    return value;
}

[[nodiscard]] std::string normalized_base_type(std::string value) {
    value = trim(std::move(value));
    if (value.starts_with("const ")) {
        value.erase(0, 6);
    }
    value =
        std::regex_replace(value, std::regex {R"(\s+(const|volatile)$)"}, "");
    value = std::regex_replace(value, std::regex {R"(\s*(&&|&|\*)$)"}, "");
    if (value.starts_with("class ")) {
        value.erase(0, 6);
    } else if (value.starts_with("struct ")) {
        value.erase(0, 7);
    } else if (value.starts_with("enum ")) {
        value.erase(0, 5);
    }
    return trim(std::move(value));
}

[[nodiscard]] bool is_number_type(const std::string_view type) {
    constexpr std::array numeric_types {
        "char",          "signed char",
        "unsigned char", "char8_t",
        "char16_t",      "char32_t",
        "short",         "unsigned short",
        "int",           "unsigned int",
        "long",          "unsigned long",
        "long long",     "unsigned long long",
        "float",         "double",
        "long double",   "size_t",
        "std::size_t",   "int8",
        "int16",         "int32",
        "int64",         "uint8",
        "uint16",        "uint32",
        "uint64",        "std::int8_t",
        "std::int16_t",  "std::int32_t",
        "std::int64_t",  "std::uint8_t",
        "std::uint16_t", "std::uint32_t",
        "std::uint64_t", "ets::Entity",
        "ets::TypeId",
    };
    return std::ranges::find(numeric_types, type) != numeric_types.end();
}

[[nodiscard]] std::string safe_identifier(const std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        result.push_back(std::isalnum(byte) != 0 ? character : '_');
    }
    while (result.find("__") != std::string::npos) {
        result = std::regex_replace(result, std::regex {"__+"}, "_");
    }
    return result;
}

[[nodiscard]] std::optional<std::string_view>
optional_argument(const std::string_view type) {
    constexpr std::array prefixes {
        std::string_view {"ets::Optional<"},
        std::string_view {"Optional<"},
        std::string_view {"std::optional<"},
    };
    for (const auto prefix : prefixes) {
        if (type.starts_with(prefix) && type.ends_with('>')) {
            return type.substr(prefix.size(), type.size() - prefix.size() - 1);
        }
    }
    return std::nullopt;
}

} // namespace

TypeMapper::TypeMapper(const Database& database) {
    std::unordered_set<std::string> ambiguous_names;
    const auto add_reflected_type = [&](const std::string& cpp_name,
                                        const std::string& luau_name) {
        const auto qualified_inserted =
            m_reflected_types.emplace(cpp_name, luau_name).second;
        if (!qualified_inserted) {
            return;
        }
        const auto separator = cpp_name.rfind("::");
        const auto unqualified =
            cpp_name.substr(separator == std::string::npos ? 0 : separator + 2);
        if (ambiguous_names.contains(unqualified)) {
            return;
        }
        const auto [existing, inserted] =
            m_unqualified_reflected_types.emplace(unqualified, luau_name);
        if (!inserted) {
            m_unqualified_reflected_types.erase(existing);
            ambiguous_names.insert(unqualified);
        }
    };
    for (const auto& cls : database.classes) {
        add_reflected_type(cls.cpp_name, cls.name);
    }
    for (const auto& enm : database.enums) {
        add_reflected_type(enm.cpp_name, enm.name);
    }
}

std::string TypeMapper::map(const std::string_view cpp_type) {
    const auto original = trim(std::string {cpp_type});
    const auto base = normalized_base_type(original);
    const auto find_reflected =
        [&](const std::string_view type) -> const std::string* {
        if (const auto reflected = m_reflected_types.find(std::string {type});
            reflected != m_reflected_types.end()) {
            return &reflected->second;
        }
        if (const auto reflected =
                m_unqualified_reflected_types.find(std::string {type});
            reflected != m_unqualified_reflected_types.end()) {
            return &reflected->second;
        }
        return nullptr;
    };
    if (const auto argument = optional_argument(base)) {
        auto mapped = map(*argument);
        if (mapped == "any") {
            m_unsupported_types.insert(original);
            return mapped;
        }
        if (!mapped.ends_with('?')) {
            mapped += '?';
        }
        return mapped;
    }
    const bool pointer = original.find('*') != std::string::npos;
    if (pointer) {
        if (const auto* reflected = find_reflected(base)) {
            return *reflected + '?';
        }
        m_unsupported_types.insert(original);
        return "any";
    }
    if (base == "void") {
        return "()";
    }
    if (base == "bool") {
        return "boolean";
    }
    if (is_number_type(base)) {
        return "number";
    }
    if (base == "std::string" || base == "std::basic_string<char>" ||
        base == "std::string_view" || base == "std::basic_string_view<char>") {
        return "string";
    }
    if (const auto* reflected = find_reflected(base)) {
        return *reflected;
    }
    m_unsupported_types.insert(original);
    return "any";
}

const std::set<std::string>& TypeMapper::unsupported_types() const {
    return m_unsupported_types;
}

std::string internal_type_name(const std::string_view cpp_name) {
    return "__Entisium_" + safe_identifier(cpp_name);
}

std::string internal_value_name(const std::string_view cpp_name) {
    return "__EntisiumType_" + safe_identifier(cpp_name);
}

} // namespace ets::luau_defgen
