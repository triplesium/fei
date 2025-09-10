#include "refl/registry.hpp"
#include "base/log.hpp"
#include "refl/cls.hpp"
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
    Type::DefaultConstructFunc default_construct,
    Type::CopyConstructFunc copy_construct,
    Type::DeleteFunc delete_func
) {
    if (m_types.contains(id)) {
        return m_types.at(id);
    }
    Type type(name, id, size, default_construct, copy_construct, delete_func);
    m_types.emplace(id, type);
    return m_types.at(id);
}

Type& Registry::get_type(TypeId id) {
    auto it = m_types.find(id);
    if (it == m_types.end()) {
        fatal("Type not found for id: {}", id.id());
    }
    return it->second;
}

Cls& Registry::add_cls(TypeId id) {
    Cls cls(id);
    m_classes.emplace(cls.type_id(), std::move(cls));
    return m_classes.at(id);
}

Cls& Registry::get_cls(TypeId id) {
    auto it = m_classes.find(id);
    if (it != m_classes.end()) {
        return it->second;
    }
    throw std::runtime_error("Class not found");
}

Type& type(TypeId id) {
    return Registry::instance().get_type(id);
}

const std::string& type_name(TypeId id) {
    return Registry::instance().get_type(id).name();
}

} // namespace fei
