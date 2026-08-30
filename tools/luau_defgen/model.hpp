#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ets::luau_defgen {

struct Annotation {
    std::string name;
};

struct Parameter {
    std::string name;
    std::string cpp_type;
};

struct Property {
    std::string name;
    std::string cpp_type;
};

struct Method {
    std::string name;
    std::string return_cpp_type;
    std::vector<Parameter> parameters;
    bool is_static {false};
    bool is_const {false};
};

struct Constructor {
    std::vector<Parameter> parameters;
};

struct Class {
    std::string cpp_name;
    std::string name;
    std::vector<std::string> namespace_path;
    std::string source;
    std::string module;
    bool abstract {false};
    std::vector<Annotation> annotations;
    std::vector<Property> properties;
    std::vector<Method> methods;
    std::vector<Constructor> constructors;
};

struct Enumerator {
    std::string name;
    std::string value;
};

struct Enum {
    std::string cpp_name;
    std::string name;
    std::vector<std::string> namespace_path;
    std::string source;
    std::string module;
    std::vector<Annotation> annotations;
    std::vector<Enumerator> values;
};

struct Database {
    std::vector<Class> classes;
    std::vector<Enum> enums;
};

[[nodiscard]] bool has_annotation(
    const std::vector<Annotation>& annotations,
    std::string_view name
);

} // namespace ets::luau_defgen
