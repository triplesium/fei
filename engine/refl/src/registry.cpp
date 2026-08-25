#include "refl/registry.hpp"

#include "base/log.hpp"
#include "container_method.hpp"
#include "dynamic_container_adapter.hpp"
#include "refl/cls.hpp"
#include "refl/enum.hpp"
#include "refl/type.hpp"

namespace ets {

Registry* Registry::s_instance = nullptr;
Registry& Registry::instance() {
    if (!s_instance) {
        s_instance = new Registry();
        register_dynamic_container_adapters(*s_instance);
    }
    return *s_instance;
}

Type& Registry::register_type(
    TypeId id,
    const std::string& name,
    std::size_t size,
    std::size_t align,
    TypeOps ops
) {
    auto name_it = m_type_ids_by_name.find(name);
    if (name_it != m_type_ids_by_name.end() && name_it->second != id) {
        fatal(
            "Type name collision for '{}': id {} conflicts with id {}",
            name,
            id.id(),
            name_it->second.id()
        );
    }

    auto it = m_types.find(id);
    if (it != m_types.end()) {
        if (it->second.name() != name) {
            fatal(
                "TypeId collision for id {}: '{}' conflicts with '{}'",
                id.id(),
                name,
                it->second.name()
            );
        }
        m_type_ids_by_name.emplace(name, id);
        return it->second;
    }
    Type type(name, id, size, align, ops);
    m_types.emplace(id, type);
    m_type_ids_by_name.emplace(name, id);
    return m_types.at(id);
}

Type& Registry::set_structured_type_name(
    TypeId id,
    std::initializer_list<std::string_view> namespace_path,
    std::string_view local_name
) {
    auto& registered = get_type(id);
    if (local_name.empty()) {
        fatal("Type '{}' has an empty local name", registered.name());
    }

    std::vector<std::string> path;
    path.reserve(namespace_path.size());
    for (const auto component : namespace_path) {
        if (component.empty() ||
            component.find("::") != std::string_view::npos) {
            fatal(
                "Type '{}' has invalid namespace component '{}'",
                registered.name(),
                component
            );
        }
        path.emplace_back(component);
    }

    if (registered.m_has_structured_name) {
        if (registered.m_namespace_path != path ||
            registered.m_local_name != local_name) {
            fatal(
                "Type '{}' has conflicting structured names",
                registered.name()
            );
        }
        return registered;
    }

    registered.m_namespace_path = std::move(path);
    registered.m_local_name = local_name;
    registered.m_has_structured_name = true;
    return registered;
}

Type& Registry::get_type(TypeId id) {
    auto result = try_get_type(id);
    if (!result) {
        fatal("{}", result.error().message);
    }
    return *result;
}

Optional<std::string> Registry::registered_type_name(TypeId id) const {
    auto it = m_types.find(id);
    if (it == m_types.end()) {
        return nullopt;
    }
    return it->second.name();
}

Result<Type&, RegistryError> Registry::try_get_type(TypeId id) {
    auto it = m_types.find(id);
    if (it == m_types.end()) {
        return failure(RegistryError::type_not_found(id));
    }
    return it->second;
}

Result<Type&, RegistryError>
Registry::try_get_type_exact(std::string_view name) {
    auto it = m_type_ids_by_name.find(std::string {name});
    if (it == m_type_ids_by_name.end()) {
        return failure(
            RegistryError::type_not_found(TypeId(name), std::string(name))
        );
    }

    return try_get_type(it->second);
}

Result<Type&, RegistryError> Registry::try_get_type(std::string_view name) {
    if (auto exact = try_get_type_exact(name)) {
        return *exact;
    }

    Type* match = nullptr;
    for (auto& [_, type] : m_types) {
        if (type.stripped_name() == name) {
            if (match) {
                return failure(
                    RegistryError::ambiguous_type_name(std::string {name})
                );
            }
            match = &type;
        }
    }

    if (match) {
        return *match;
    }

    return failure(
        RegistryError::type_not_found(TypeId(name), std::string(name))
    );
}

Cls& Registry::add_cls(TypeId id) {
    auto it = m_classes.find(id);
    if (it != m_classes.end()) {
        return it->second;
    }
    Cls cls(id);
    m_classes.emplace(cls.type_id(), std::move(cls));
    return m_classes.at(id);
}

Cls& Registry::get_cls(TypeId id) {
    auto result = try_get_cls(id);
    if (!result) {
        fatal("{}", result.error().message);
    }
    return *result;
}

Result<Cls&, RegistryError> Registry::try_get_cls(TypeId id) {
    auto it = m_classes.find(id);
    if (it == m_classes.end()) {
        return failure(
            RegistryError::class_not_found(id, registered_type_name(id))
        );
    }
    return it->second;
}

Enum& Registry::add_enum(TypeId id) {
    auto it = m_enums.find(id);
    if (it != m_enums.end()) {
        return it->second;
    }
    Enum enum_info(id);
    m_enums.emplace(id, std::move(enum_info));
    return m_enums.at(id);
}

Enum& Registry::get_enum(TypeId id) {
    auto result = try_get_enum(id);
    if (!result) {
        fatal("{}", result.error().message);
    }
    return *result;
}

Result<Enum&, RegistryError> Registry::try_get_enum(TypeId id) {
    auto it = m_enums.find(id);
    if (it == m_enums.end()) {
        return failure(
            RegistryError::enum_not_found(id, registered_type_name(id))
        );
    }
    return it->second;
}

GenericType&
Registry::register_generic_type(TypeId id, GenericType generic_type) {
    auto it = m_generic_types.find(id);
    if (it != m_generic_types.end()) {
        return it->second;
    }
    if (generic_type.specialized_type_id != id) {
        fatal(
            "Generic type id mismatch: registered id {} but generic type "
            "reports {}",
            id.id(),
            generic_type.specialized_type_id.id()
        );
    }
    auto inserted = m_generic_types.emplace(id, std::move(generic_type));
    return inserted.first->second;
}

GenericType& Registry::get_generic_type(TypeId id) {
    auto result = try_get_generic_type(id);
    if (!result) {
        fatal("{}", result.error().message);
    }
    return *result;
}

Result<GenericType&, RegistryError> Registry::try_get_generic_type(TypeId id) {
    auto it = m_generic_types.find(id);
    if (it == m_generic_types.end()) {
        return failure(
            RegistryError::generic_type_not_found(id, registered_type_name(id))
        );
    }
    return it->second;
}

ContainerAdapter& Registry::register_container_adapter(
    TypeId id,
    std::unique_ptr<ContainerAdapter> adapter
) {
    auto it = m_container_adapters.find(id);
    if (it != m_container_adapters.end()) {
        register_container_methods(add_cls(id), *it->second);
        return *it->second;
    }
    if (!adapter) {
        fatal("Cannot register null container adapter for id {}", id.id());
    }
    if (adapter->container_type() != id) {
        fatal(
            "Container adapter type id mismatch: registered id {} but adapter "
            "reports {}",
            id.id(),
            adapter->container_type().id()
        );
    }
    auto inserted = m_container_adapters.emplace(id, std::move(adapter));
    auto& registered = *inserted.first->second;
    register_container_methods(add_cls(id), registered);
    return registered;
}

ContainerAdapter& Registry::get_container_adapter(TypeId id) {
    auto result = try_get_container_adapter(id);
    if (!result) {
        fatal("{}", result.error().message);
    }
    return *result;
}

Result<ContainerAdapter&, RegistryError>
Registry::try_get_container_adapter(TypeId id) {
    auto it = m_container_adapters.find(id);
    if (it == m_container_adapters.end()) {
        return failure(
            RegistryError::container_adapter_not_found(
                id,
                registered_type_name(id)
            )
        );
    }
    return *it->second;
}

Type& Registry::add_generated_tag(TypeId type_id, std::string tag) {
    const TypeTagId tag_id {std::string_view {tag}};
    auto existing = m_tag_names.find(tag_id);
    if (existing != m_tag_names.end() && existing->second != tag) {
        fatal(
            "Type tag collision for id {}: '{}' conflicts with '{}'",
            tag_id.id(),
            tag,
            existing->second
        );
    }

    m_tag_names.emplace(tag_id, std::move(tag));
    auto& registered_type = get_type(type_id);
    if (registered_type.has_tag(tag_id) && registered_type.tag_value(tag_id)) {
        fatal(
            "Type '{}' reflection tag '{}' is declared both with and without "
            "a value",
            registered_type.name(),
            *tag_name(tag_id)
        );
    }
    registered_type.add_tag(tag_id);
    return registered_type;
}

Type& Registry::add_generated_tag(
    TypeId type_id,
    std::string tag,
    std::string value
) {
    const TypeTagId tag_id {std::string_view {tag}};
    auto existing = m_tag_names.find(tag_id);
    if (existing != m_tag_names.end() && existing->second != tag) {
        fatal(
            "Type tag collision for id {}: '{}' conflicts with '{}'",
            tag_id.id(),
            tag,
            existing->second
        );
    }

    m_tag_names.emplace(tag_id, std::move(tag));
    auto& registered_type = get_type(type_id);
    if (registered_type.has_tag(tag_id)) {
        const auto existing_value = registered_type.tag_value(tag_id);
        if (!existing_value || *existing_value != value) {
            fatal(
                "Type '{}' reflection tag '{}' has conflicting values",
                registered_type.name(),
                *tag_name(tag_id)
            );
        }
        return registered_type;
    }
    registered_type.add_tag(tag_id);
    registered_type.set_tag_value(tag_id, std::move(value));
    return registered_type;
}

Type& Registry::add_generated_annotation(
    TypeId type_id,
    std::string annotation
) {
    auto& registered_type = add_generated_tag(type_id, annotation);
    registered_type.add_annotation(std::move(annotation));
    return registered_type;
}

Type& Registry::add_generated_annotation_field(
    TypeId type_id,
    std::string annotation,
    std::string field,
    std::string value
) {
    auto& registered_type = add_generated_annotation(type_id, annotation);
    const std::string tag = annotation + "." + field;
    add_generated_tag(type_id, tag, value);
    registered_type.set_annotation_field(
        std::move(annotation),
        std::move(field),
        std::move(value)
    );
    return registered_type;
}

Optional<std::string_view> Registry::tag_name(TypeTagId tag) const {
    auto it = m_tag_names.find(tag);
    if (it == m_tag_names.end()) {
        return nullopt;
    }
    return std::string_view {it->second};
}

std::vector<TypeId> Registry::types_with_tag(TypeTagId tag) const {
    std::vector<TypeId> result;
    for (const auto& [id, reflected_type] : m_types) {
        if (reflected_type.has_tag(tag)) {
            result.push_back(id);
        }
    }
    std::ranges::sort(result);
    return result;
}

std::vector<TypeId>
Registry::types_with_annotation(std::string_view annotation) const {
    std::vector<TypeId> result;
    for (const auto& [id, reflected_type] : m_types) {
        if (reflected_type.has_annotation(annotation)) {
            result.push_back(id);
        }
    }
    std::ranges::sort(result);
    return result;
}

bool Registry::has_enum(TypeId id) const {
    return m_enums.contains(id);
}

void Registry::clear_generated_metadata() {
    ++m_class_epoch;
    m_classes.clear();
    m_enums.clear();
    m_tag_names.clear();
    for (auto& [_, reflected_type] : m_types) {
        reflected_type.clear_tags();
    }

    for (const auto& [id, adapter] : m_container_adapters) {
        register_container_methods(add_cls(id), *adapter);
    }
}

Type& type(TypeId id) {
    return Registry::instance().get_type(id);
}

const std::string& type_name(TypeId id) {
    return Registry::instance().get_type(id).name();
}

bool is_enum_type(TypeId type_id) {
    return Registry::instance().has_enum(type_id);
}

} // namespace ets
