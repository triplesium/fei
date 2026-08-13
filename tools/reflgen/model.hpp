#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fei::reflgen {

struct ReflectionTag {
    std::string key;
    std::optional<std::string> value;
    std::optional<std::string> group;
    std::optional<std::string> field;
};

struct ParamInfo {
    std::string name;
    std::string type_name;
};

struct MemberInfo {
    std::string name;
    std::string type_name;
    std::string access;
};

struct MethodInfo : MemberInfo {
    std::vector<ParamInfo> parameters;
    bool is_static = false;
    bool is_const = false;
    std::string ref_qualifier;
    bool is_abstract = false;

    [[nodiscard]] std::string to_cpp_type(std::string_view parent) const;
};

struct ClassInfo {
    std::string name;
    std::string source_file;
    std::vector<ReflectionTag> tags;
    std::vector<MemberInfo> properties;
    std::vector<MethodInfo> methods;
    std::vector<MethodInfo> constructors;

    [[nodiscard]] bool is_abstract() const;
};

struct EnumValueInfo {
    std::string name;
    long long value = 0;
};

struct EnumInfo {
    std::string name;
    std::string source_file;
    std::vector<ReflectionTag> tags;
    std::string underlying_type;
    bool is_scoped = false;
    std::vector<EnumValueInfo> values;
};

struct ParseResult {
    std::vector<ClassInfo> classes;
    std::vector<EnumInfo> enums;
};

void dedupe_reflected_types(ParseResult& result);
void filter_codegen_unsupported_members(ParseResult& result);

} // namespace fei::reflgen
