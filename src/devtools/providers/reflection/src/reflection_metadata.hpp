#pragma once

#include "base/result.hpp"
#include "base/types.hpp"
#include "devtools/type_selector.hpp"
#include "refl/reflect.hpp"

#include <string>
#include <vector>

namespace fei::devtools::reflection {

inline constexpr uint32 c_default_search_limit = 50;
inline constexpr uint32 c_max_search_limit = 200;

FEI_REFLECT()
struct SearchRequest {
    std::string pattern;
    uint32 limit {c_default_search_limit};
};

FEI_REFLECT()
struct TypeSummary {
    std::string id;
    std::string name;
    std::vector<std::string> facets;
};

FEI_REFLECT()
struct SearchResponse {
    std::vector<TypeSummary> matches;
    bool truncated {false};
};

FEI_REFLECT()
struct DescribeRequest {
    std::string type;
};

FEI_REFLECT()
struct TypeReference {
    std::string id;
    std::string name;
};

FEI_REFLECT()
struct QualifiedTypeDescriptor {
    TypeReference type;
    bool is_const {false};
    bool is_pointer {false};
    bool is_lvalue_reference {false};
    bool is_rvalue_reference {false};
};

FEI_REFLECT()
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

FEI_REFLECT()
struct ParameterDescriptor {
    std::string name;
    QualifiedTypeDescriptor type;
};

FEI_REFLECT()
struct PropertyDescriptor {
    std::string name;
    TypeReference type;
};

FEI_REFLECT()
struct MethodDescriptor {
    std::string name;
    std::vector<ParameterDescriptor> parameters;
    QualifiedTypeDescriptor return_type;
    bool is_const {false};
    bool is_static {false};
};

FEI_REFLECT()
struct ConstructorDescriptor {
    std::vector<ParameterDescriptor> parameters;
};

FEI_REFLECT()
struct EnumValueDescriptor {
    std::string name;
    std::string value;
};

FEI_REFLECT()
struct GenericArgumentDescriptor {
    std::string kind;
    TypeReference type;
    std::string value;
};

FEI_REFLECT()
struct GenericDescriptor {
    bool present {false};
    std::string name;
    std::string id;
    std::vector<GenericArgumentDescriptor> arguments;
};

FEI_REFLECT()
struct ContainerDescriptor {
    bool present {false};
    std::string kind;
    TypeReference element_type;
    TypeReference key_type;
    TypeReference mapped_type;
    bool fixed_size {false};
};

FEI_REFLECT()
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

using ::fei::devtools::format_type_id;

} // namespace fei::devtools::reflection
