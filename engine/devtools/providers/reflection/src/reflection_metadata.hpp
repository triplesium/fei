#pragma once

#include "base/result.hpp"
#include "base/types.hpp"
#include "devtools/type_selector.hpp"
#include "refl/reflect.hpp"

#include <string>
#include <vector>

namespace ets::devtools::reflection {

inline constexpr uint32 c_default_search_limit = 50;
inline constexpr uint32 c_max_search_limit = 200;

ETS_REFLECT()
struct SearchRequest {
    std::string pattern;
    uint32 limit {c_default_search_limit};
};

ETS_REFLECT()
struct TypeSummary {
    std::string id;
    std::string name;
    std::vector<std::string> facets;
};

ETS_REFLECT()
struct SearchResponse {
    std::vector<TypeSummary> matches;
    bool truncated {false};
};

ETS_REFLECT()
struct DescribeRequest {
    std::string type;
};

ETS_REFLECT()
struct TypeReference {
    std::string id;
    std::string name;
};

ETS_REFLECT()
struct QualifiedTypeDescriptor {
    TypeReference type;
    bool is_const {false};
    bool is_pointer {false};
    bool is_lvalue_reference {false};
    bool is_rvalue_reference {false};
};

ETS_REFLECT()
struct TypeOperationsDescriptor {
    bool default_constructible {false};
    bool copy_constructible {false};
    bool move_constructible {false};
    bool copy_assignable {false};
    bool move_assignable {false};
    bool destructible {false};
    bool equality_comparable {false};
    bool hashable {false};
};

ETS_REFLECT()
struct ParameterDescriptor {
    std::string name;
    QualifiedTypeDescriptor type;
};

ETS_REFLECT()
struct PropertyDescriptor {
    std::string name;
    TypeReference type;
};

ETS_REFLECT()
struct MethodDescriptor {
    std::string name;
    std::vector<ParameterDescriptor> parameters;
    QualifiedTypeDescriptor return_type;
    bool is_const {false};
    bool is_static {false};
};

ETS_REFLECT()
struct ConstructorDescriptor {
    std::vector<ParameterDescriptor> parameters;
};

ETS_REFLECT()
struct EnumValueDescriptor {
    std::string name;
    std::string value;
};

ETS_REFLECT()
struct GenericArgumentDescriptor {
    std::string kind;
    TypeReference type;
    std::string value;
};

ETS_REFLECT()
struct GenericDescriptor {
    bool present {false};
    std::string name;
    std::string id;
    std::vector<GenericArgumentDescriptor> arguments;
};

ETS_REFLECT()
struct ContainerDescriptor {
    bool present {false};
    std::string kind;
    TypeReference element_type;
    TypeReference key_type;
    TypeReference mapped_type;
    bool fixed_size {false};
};

ETS_REFLECT()
struct TypeDescriptor {
    TypeSummary summary;
    uint64 size {0};
    uint64 alignment {0};
    TypeOperationsDescriptor operations;
    std::vector<PropertyDescriptor> properties;
    std::vector<MethodDescriptor> methods;
    std::vector<ConstructorDescriptor> constructors;
    std::vector<EnumValueDescriptor> enum_values;
    GenericDescriptor generic;
    ContainerDescriptor container;
    bool dynamic {false};
};

struct ReflectionError {
    int status {500};
    std::string message;
};

Result<SearchResponse, ReflectionError>
search_types(const SearchRequest& request);
Result<TypeDescriptor, ReflectionError>
describe_type(const DescribeRequest& request);

using ::ets::devtools::format_type_id;

} // namespace ets::devtools::reflection
