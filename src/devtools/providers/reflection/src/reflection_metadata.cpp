#include "reflection_metadata.hpp"

#include "refl/callable.hpp"
#include "refl/cls.hpp"
#include "refl/container_adapter.hpp"
#include "refl/enum.hpp"
#include "refl/generic_type.hpp"
#include "refl/method.hpp"
#include "refl/property.hpp"
#include "refl/qual_type.hpp"
#include "refl/registry.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace fei::devtools::reflection {
namespace {

constexpr std::size_t c_max_pattern_length = 256;

std::string lowercase(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (unsigned char character : value) {
        result.push_back(static_cast<char>(std::tolower(character)));
    }
    return result;
}

TypeReference make_type_reference(TypeId id) {
    if (!id) {
        return {};
    }
    TypeReference result {.id = format_type_id(id)};
    if (auto type = Registry::instance().try_get_type(id)) {
        result.name = type->name();
    }
    return result;
}

QualifiedTypeDescriptor make_qualified_type(QualType type) {
    return QualifiedTypeDescriptor {
        .type = make_type_reference(type.type_id()),
        .is_const = type.is_const(),
        .is_pointer = type.is_pointer(),
        .is_lvalue_reference = type.is_reference(),
        .is_rvalue_reference = type.is_rvalue_reference(),
    };
}

std::vector<std::string> type_facets(TypeId id) {
    auto& registry = Registry::instance();
    std::vector<std::string> facets;
    if (registry.try_get_cls(id)) {
        facets.emplace_back("class");
    }
    if (registry.has_enum(id)) {
        facets.emplace_back("enum");
    }
    if (registry.try_get_generic_type(id)) {
        facets.emplace_back("generic");
    }
    if (registry.try_get_container_adapter(id)) {
        facets.emplace_back("container");
    }
    if (registry.try_get_dynamic_struct_layout(id) != nullptr) {
        facets.emplace_back("dynamic");
    }
    if (facets.empty()) {
        facets.emplace_back("plain");
    }
    return facets;
}

TypeSummary make_type_summary(const Type& type) {
    return TypeSummary {
        .id = format_type_id(type.id()),
        .name = type.name(),
        .facets = type_facets(type.id()),
    };
}

std::vector<ParameterDescriptor>
make_parameters(const std::vector<Param>& parameters) {
    std::vector<ParameterDescriptor> result;
    result.reserve(parameters.size());
    for (const auto& parameter : parameters) {
        result.push_back(
            ParameterDescriptor {
                .name = parameter.name(),
                .type = make_qualified_type(parameter.type()),
            }
        );
    }
    return result;
}

std::string
parameters_sort_key(const std::vector<ParameterDescriptor>& parameters) {
    std::string key;
    for (const auto& parameter : parameters) {
        key += parameter.type.type.name;
        key += ':';
        key += std::to_string(
            static_cast<unsigned int>(parameter.type.is_const) |
            static_cast<unsigned int>(parameter.type.is_pointer) << 1U |
            static_cast<unsigned int>(parameter.type.is_lvalue_reference)
                << 2U |
            static_cast<unsigned int>(parameter.type.is_rvalue_reference) << 3U
        );
        key += ';';
    }
    return key;
}

const char* container_kind_name(ContainerKind kind) {
    switch (kind) {
        case ContainerKind::Sequence:
            return "sequence";
        case ContainerKind::Optional:
            return "optional";
        case ContainerKind::Product:
            return "product";
        case ContainerKind::Map:
            return "map";
        case ContainerKind::Set:
            return "set";
    }
    return "unsupported";
}

GenericDescriptor make_generic_descriptor(TypeId id) {
    auto generic = Registry::instance().try_get_generic_type(id);
    if (!generic) {
        return {};
    }

    GenericDescriptor result {
        .present = true,
        .name = generic->generic_name,
        .id = format_type_id(generic->generic_type_id),
    };
    result.arguments.reserve(generic->arguments.size());
    for (const auto& argument : generic->arguments) {
        switch (argument.kind) {
            case GenericArgument::Kind::Type:
                result.arguments.push_back(
                    GenericArgumentDescriptor {
                        .kind = "type",
                        .type = make_type_reference(argument.type_id),
                    }
                );
                break;
            case GenericArgument::Kind::SignedInteger:
                result.arguments.push_back(
                    GenericArgumentDescriptor {
                        .kind = "signed_integer",
                        .value = std::to_string(argument.signed_integer),
                    }
                );
                break;
            case GenericArgument::Kind::UnsignedInteger:
                result.arguments.push_back(
                    GenericArgumentDescriptor {
                        .kind = "unsigned_integer",
                        .value = std::to_string(argument.unsigned_integer),
                    }
                );
                break;
        }
    }
    return result;
}

ContainerDescriptor make_container_descriptor(TypeId id) {
    auto adapter = Registry::instance().try_get_container_adapter(id);
    if (!adapter) {
        return {};
    }

    ContainerDescriptor result {
        .present = true,
        .kind = container_kind_name(adapter->kind()),
    };
    if (const auto* indexed = adapter->indexed()) {
        result.element_type = make_type_reference(indexed->element_type());
        result.fixed_size = indexed->fixed_size();
    }
    if (const auto* associative = adapter->associative()) {
        result.key_type = make_type_reference(associative->key_type());
        if (associative->has_mapped_value()) {
            result.mapped_type =
                make_type_reference(associative->mapped_type());
        }
    }
    return result;
}

Result<Type&, ReflectionError> resolve_type(std::string_view selector) {
    auto& registry = Registry::instance();
    if (selector.starts_with("0x") || selector.starts_with("0X")) {
        uint64 id {};
        auto digits = selector.substr(2);
        auto [end, error] = std::from_chars(
            digits.data(),
            digits.data() + digits.size(),
            id,
            16
        );
        if (digits.empty() || error != std::errc {} ||
            end != digits.data() + digits.size()) {
            return failure(
                ReflectionError {400, "Invalid hexadecimal type id"}
            );
        }
        auto type = registry.try_get_type(TypeId {id});
        if (!type) {
            return failure(ReflectionError {404, type.error().message});
        }
        return *type;
    }

    auto type = registry.try_get_type(selector);
    if (!type) {
        auto status =
            type.error().kind == RegistryError::Kind::AmbiguousTypeName ? 409 :
                                                                          404;
        return failure(ReflectionError {status, type.error().message});
    }
    return *type;
}

} // namespace

std::string format_type_id(TypeId id) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setfill('0') << std::setw(16) << id.id();
    return stream.str();
}

Result<SearchResponse, ReflectionError>
search_types(const SearchRequest& request) {
    if (request.pattern.size() > c_max_pattern_length) {
        return failure(ReflectionError {400, "Search pattern is too long"});
    }
    if (request.limit == 0 || request.limit > c_max_search_limit) {
        return failure(
            ReflectionError {
                400,
                "Search limit must be between 1 and " +
                    std::to_string(c_max_search_limit),
            }
        );
    }

    const auto pattern = lowercase(request.pattern);
    std::vector<const Type*> matched;
    for (const auto& [id, type] : Registry::instance().types()) {
        (void)id;
        auto full_name = lowercase(type.name());
        auto short_name = lowercase(type.stripped_name());
        if (pattern.empty() || full_name.contains(pattern) ||
            short_name.contains(pattern)) {
            matched.push_back(&type);
        }
    }
    std::ranges::sort(matched, {}, [](const Type* type) {
        return type->name();
    });

    SearchResponse response {.truncated = matched.size() > request.limit};
    const auto result_count =
        std::min<std::size_t>(matched.size(), request.limit);
    response.matches.reserve(result_count);
    for (std::size_t index = 0; index < result_count; ++index) {
        response.matches.push_back(make_type_summary(*matched[index]));
    }
    return response;
}

Result<TypeDescriptor, ReflectionError>
describe_type(const DescribeRequest& request) {
    if (request.type.empty()) {
        return failure(
            ReflectionError {400, "Type selector must not be empty"}
        );
    }
    auto resolved = resolve_type(request.type);
    if (!resolved) {
        return failure(std::move(resolved.error()));
    }

    auto& type = *resolved;
    TypeDescriptor result {
        .summary = make_type_summary(type),
        .size = type.size(),
        .alignment = type.align(),
        .operations =
            TypeOperationsDescriptor {
                .default_constructible = type.default_constructible(),
                .copy_constructible = type.copy_constructible(),
                .move_constructible = type.move_constructible(),
                .copy_assignable = type.copy_assignable(),
                .move_assignable = type.move_assignable(),
                .destructible = type.destructible(),
                .equality_comparable = type.equality_comparable(),
                .hashable = type.hashable(),
            },
        .generic = make_generic_descriptor(type.id()),
        .container = make_container_descriptor(type.id()),
        .dynamic =
            Registry::instance().try_get_dynamic_struct_layout(type.id()) !=
            nullptr,
    };

    if (auto reflected = Registry::instance().try_get_cls(type.id())) {
        for (const auto* property : reflected->get_properties()) {
            result.properties.push_back(
                PropertyDescriptor {
                    .name = property->name(),
                    .type = make_type_reference(property->type_id()),
                }
            );
        }
        std::ranges::sort(result.properties, {}, &PropertyDescriptor::name);

        for (const auto* method : reflected->get_methods()) {
            result.methods.push_back(
                MethodDescriptor {
                    .name = method->name(),
                    .parameters = make_parameters(method->params()),
                    .return_type = make_qualified_type(method->return_type()),
                    .is_const = method->is_const(),
                    .is_static = method->is_static(),
                }
            );
        }
        std::ranges::sort(
            result.methods,
            [](const MethodDescriptor& left, const MethodDescriptor& right) {
                return std::tuple {
                           left.name,
                           parameters_sort_key(left.parameters),
                           left.is_const,
                           left.is_static,
                       } < std::tuple {
                               right.name,
                               parameters_sort_key(right.parameters),
                               right.is_const,
                               right.is_static,
                           };
            }
        );

        for (const auto* constructor : reflected->get_constructors()) {
            result.constructors.push_back(
                ConstructorDescriptor {
                    .parameters = make_parameters(constructor->params()),
                }
            );
        }
        std::ranges::sort(
            result.constructors,
            [](const ConstructorDescriptor& left,
               const ConstructorDescriptor& right) {
                return parameters_sort_key(left.parameters) <
                       parameters_sort_key(right.parameters);
            }
        );
    }

    if (auto reflected = Registry::instance().try_get_enum(type.id())) {
        for (const auto& [name, value] : reflected->enumerators()) {
            result.enum_values.push_back(
                EnumValueDescriptor {
                    .name = name,
                    .value = std::to_string(value),
                }
            );
        }
        std::ranges::sort(result.enum_values, {}, &EnumValueDescriptor::name);
    }

    return result;
}

} // namespace fei::devtools::reflection
