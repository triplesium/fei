#include "refl/registry.hpp"

#include "base/log.hpp"
#include "refl/cls.hpp"
#include "refl/enum.hpp"
#include "refl/type.hpp"

namespace fei {

Registry* Registry::s_instance = nullptr;
Registry& Registry::instance() {
    if (!s_instance) {
        s_instance = new Registry();
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
    return *inserted.first->second;
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

bool Registry::has_enum(TypeId id) const {
    return m_enums.contains(id);
}

void Registry::clear_generated_metadata() {
    m_classes.clear();
    m_enums.clear();
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

} // namespace fei
