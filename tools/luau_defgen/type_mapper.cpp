#include "type_mapper.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <regex>

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

} // namespace

TypeMapper::TypeMapper(const Database& database) {
    for (const auto& cls : database.classes) {
        m_reflected_types.emplace(cls.cpp_name, cls.name);
    }
    for (const auto& enm : database.enums) {
        m_reflected_types.emplace(enm.cpp_name, enm.name);
    }
}

std::string TypeMapper::map(const std::string_view cpp_type) {
    const auto original = trim(std::string {cpp_type});
    const auto base = normalized_base_type(original);
    const bool pointer = original.find('*') != std::string::npos;
    if (pointer) {
        if (const auto reflected = m_reflected_types.find(base);
            reflected != m_reflected_types.end()) {
            return reflected->second + '?';
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
    if (const auto reflected = m_reflected_types.find(base);
        reflected != m_reflected_types.end()) {
        return reflected->second;
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
