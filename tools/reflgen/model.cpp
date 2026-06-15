#include "model.hpp"

#include <algorithm>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>

namespace fei::reflgen {
namespace {

const std::unordered_set<std::string> primitive_bindable_types = {
    "void",
    "bool",
    "char",
    "signed char",
    "unsigned char",
    "short",
    "short int",
    "unsigned short",
    "unsigned short int",
    "int",
    "unsigned int",
    "long",
    "long int",
    "unsigned long",
    "unsigned long int",
    "long long",
    "long long int",
    "unsigned long long",
    "unsigned long long int",
    "float",
    "double",
    "long double",
    "size_t",
    "std::size_t",
    "int8_t",
    "std::int8_t",
    "uint8_t",
    "std::uint8_t",
    "int16_t",
    "std::int16_t",
    "uint16_t",
    "std::uint16_t",
    "int32_t",
    "std::int32_t",
    "uint32_t",
    "std::uint32_t",
    "int64_t",
    "std::int64_t",
    "uint64_t",
    "std::uint64_t",
};

const std::unordered_set<std::string> special_bindable_types = {
    "std::string",
    "string",
    "fei::TypeId",
    "TypeId",
    "fei::Ref",
    "Ref",
};

[[nodiscard]] bool starts_with(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() &&
           text.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] bool ends_with(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() &&
           text.substr(text.size() - suffix.size()) == suffix;
}

[[nodiscard]] std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

[[nodiscard]] std::string collapse_spaces(std::string_view text) {
    std::istringstream stream {std::string(text)};
    std::string word;
    std::string result;
    while (stream >> word) {
        if (!result.empty()) {
            result += ' ';
        }
        result += word;
    }
    return result;
}

[[nodiscard]] std::string stripped_cpp_name(std::string_view name) {
    const auto text = std::string(name);
    const auto pos = text.rfind("::");
    if (pos == std::string::npos) {
        return text;
    }
    return text.substr(pos + 2);
}

[[nodiscard]] std::string strip_cv_ref(std::string_view type_name) {
    auto type = collapse_spaces(trim(type_name));
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto* prefix :
             {"const ", "volatile ", "class ", "struct ", "enum "}) {
            if (starts_with(type, prefix)) {
                type = trim(
                    std::string_view(type).substr(
                        std::string_view(prefix).size()
                    )
                );
                changed = true;
            }
        }
        for (const auto* suffix : {" const", " volatile", "&&", "&"}) {
            if (ends_with(type, suffix)) {
                type = trim(
                    std::string_view(type).substr(
                        0,
                        type.size() - std::string_view(suffix).size()
                    )
                );
                changed = true;
            }
        }
    }
    return type;
}

[[nodiscard]] bool contains(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

[[nodiscard]] bool is_bindable_type(
    std::string_view type_name,
    const std::unordered_set<std::string>& reflectable_type_names,
    bool allow_void
) {
    const auto type = strip_cv_ref(type_name);
    if (type.empty()) {
        return false;
    }
    if (type == "void") {
        return allow_void;
    }
    if (contains(type, "*") || contains(type, "[") || contains(type, "(*)")) {
        return false;
    }
    if (contains(type, "<") || contains(type, ">")) {
        return false;
    }
    if (primitive_bindable_types.contains(type) ||
        special_bindable_types.contains(type)) {
        return true;
    }
    if (reflectable_type_names.contains(type)) {
        return true;
    }
    return reflectable_type_names.contains(stripped_cpp_name(type));
}

[[nodiscard]] std::string
join_param_types(const std::vector<ParamInfo>& parameters) {
    std::string result;
    for (const auto& param : parameters) {
        if (!result.empty()) {
            result += ", ";
        }
        result += param.type_name;
    }
    return result;
}

} // namespace

std::string MethodInfo::to_cpp_type(std::string_view parent) const {
    const auto param_types = join_param_types(parameters);
    if (is_static) {
        return type_name + "(*)(" + param_types + ")";
    }

    std::string qualifier_suffix;
    if (is_const) {
        qualifier_suffix += " const";
    }
    if (!ref_qualifier.empty()) {
        qualifier_suffix += " " + ref_qualifier;
    }

    const std::regex function_pointer_return_regex(R"(^(.+)\(\*\)\((.*)\)$)");
    std::smatch match;
    if (std::regex_match(type_name, match, function_pointer_return_regex)) {
        return trim(match[1].str()) + " (* (" + std::string(parent) + "::*)(" +
               param_types + ")" + qualifier_suffix + ")(" +
               trim(match[2].str()) + ")";
    }

    return type_name + "(" + std::string(parent) + "::*)(" + param_types + ")" +
           qualifier_suffix;
}

bool ClassInfo::is_abstract() const {
    return std::ranges::any_of(methods, [](const MethodInfo& method) {
        return method.is_abstract;
    });
}

void dedupe_reflected_types(ParseResult& result) {
    std::unordered_set<std::string> seen_classes;
    std::vector<ClassInfo> classes;
    for (auto& cls : result.classes) {
        if (seen_classes.insert(cls.name).second) {
            classes.push_back(std::move(cls));
        }
    }
    result.classes = std::move(classes);

    std::unordered_set<std::string> seen_enums;
    std::vector<EnumInfo> enums;
    for (auto& enum_info : result.enums) {
        if (seen_enums.insert(enum_info.name).second) {
            enums.push_back(std::move(enum_info));
        }
    }
    result.enums = std::move(enums);
}

void filter_bindable_members(ParseResult& result) {
    std::unordered_set<std::string> reflectable_type_names;
    for (const auto& cls : result.classes) {
        reflectable_type_names.insert(cls.name);
        reflectable_type_names.insert(stripped_cpp_name(cls.name));
    }
    for (const auto& enum_info : result.enums) {
        reflectable_type_names.insert(enum_info.name);
        reflectable_type_names.insert(stripped_cpp_name(enum_info.name));
    }

    for (auto& cls : result.classes) {
        std::vector<MemberInfo> properties;
        for (auto& prop : cls.properties) {
            if (is_bindable_type(
                    prop.type_name,
                    reflectable_type_names,
                    false
                )) {
                properties.push_back(std::move(prop));
            } else if (prop.access == "public") {
                std::cerr << "Skipping " << cls.name << "." << prop.name
                          << ": unbindable property type " << prop.type_name
                          << '\n';
            }
        }
        cls.properties = std::move(properties);

        std::vector<MethodInfo> methods;
        for (auto& method : cls.methods) {
            const auto return_ok = is_bindable_type(
                method.type_name,
                reflectable_type_names,
                true
            );
            const auto params_ok = std::ranges::all_of(
                method.parameters,
                [&](const ParamInfo& param) {
                    return is_bindable_type(
                        param.type_name,
                        reflectable_type_names,
                        false
                    );
                }
            );
            if (return_ok && params_ok) {
                methods.push_back(std::move(method));
            } else if (method.access == "public") {
                std::cerr << "Skipping " << cls.name << "." << method.name
                          << "(" << join_param_types(method.parameters)
                          << "): unbindable return/parameter type\n";
            }
        }
        cls.methods = std::move(methods);

        std::vector<MethodInfo> constructors;
        for (auto& constructor : cls.constructors) {
            const auto params_ok = std::ranges::all_of(
                constructor.parameters,
                [&](const ParamInfo& param) {
                    return is_bindable_type(
                        param.type_name,
                        reflectable_type_names,
                        false
                    );
                }
            );
            if (params_ok) {
                constructors.push_back(std::move(constructor));
            } else if (constructor.access == "public") {
                std::cerr << "Skipping " << cls.name << " constructor("
                          << join_param_types(constructor.parameters)
                          << "): unbindable parameter type\n";
            }
        }
        cls.constructors = std::move(constructors);
    }
}

} // namespace fei::reflgen
