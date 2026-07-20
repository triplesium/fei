#pragma once

#include "base/result.hpp"
#include "base/types.hpp"
#include "refl/reflect.hpp"
#include "refl/type.hpp"

#include <string>
#include <vector>

namespace fei::devtools::reflection {

inline constexpr uint32 c_default_search_limit = 50;
inline constexpr uint32 c_max_search_limit = 200;

struct FEI_REFLECT SearchRequest {
    std::string pattern;
    uint32 limit {c_default_search_limit};
};

struct FEI_REFLECT TypeSummary {
    std::string id;
    std::string name;
    std::vector<std::string> facets;
};

struct FEI_REFLECT SearchResponse {
    std::vector<TypeSummary> matches;
    bool truncated {false};
};

struct FEI_REFLECT DescribeRequest {
    std::string type;
};

struct FEI_REFLECT TypeReference {
    std::string id;
    std::string name;
};

struct FEI_REFLECT QualifiedTypeDescriptor {
    TypeReference type;
    bool is_const {false};
    bool is_pointer {false};
    bool is_lvalue_reference {false};
    bool is_rvalue_reference {false};
};

struct FEI_REFLECT TypeOperationsDescriptor {
    bool default_constructible {false};
    bool copy_constructible {false};
    bool move_constructible {false};
    bool copy_assignable {false};
    bool move_assignable {false};
    bool destructible {false};
    bool equality_comparable {false};
    bool hashable {false};
};

struct FEI_REFLECT ParameterDescriptor {
    std::string name;
    QualifiedTypeDescriptor type;
};

struct FEI_REFLECT PropertyDescriptor {
    std::string name;
    TypeReference type;
};

struct FEI_REFLECT MethodDescriptor {
    std::string name;
    std::vector<ParameterDescriptor> parameters;
    QualifiedTypeDescriptor return_type;
    bool is_const {false};
    bool is_static {false};
};

struct FEI_REFLECT ConstructorDescriptor {
    std::vector<ParameterDescriptor> parameters;
};

struct FEI_REFLECT EnumValueDescriptor {
    std::string name;
    std::string value;
};

struct FEI_REFLECT GenericArgumentDescriptor {
    std::string kind;
    TypeReference type;
    std::string value;
};

struct FEI_REFLECT GenericDescriptor {
    bool present {false};
    std::string name;
    std::string id;
    std::vector<GenericArgumentDescriptor> arguments;
};

struct FEI_REFLECT ContainerDescriptor {
    bool present {false};
    std::string kind;
    TypeReference element_type;
    TypeReference key_type;
    TypeReference mapped_type;
    bool fixed_size {false};
};

struct FEI_REFLECT TypeDescriptor {
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

std::string format_type_id(TypeId id);

} // namespace fei::devtools::reflection
