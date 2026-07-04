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
        return it->second;
    }
    Type type(name, id, size, align, ops);
    m_types.emplace(id, type);
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

Result<Type&, RegistryError> Registry::try_get_type(std::string_view name) {
    for (auto& [_, type] : m_types) {
        if (type.name() == name) {
            return type;
        }
    }
    for (auto& [_, type] : m_types) {
        if (type.stripped_name() == name) {
            return type;
        }
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
