#include "refl/enum.hpp"

#include "base/log.hpp"
#include "refl/registry.hpp"
#include "refl/val.hpp"

namespace fei {

Val Enum::make_val(std::int64_t underlying_value) const {
    auto& type = Registry::instance().get_type(m_type_id);
    if (!m_construct) {
        error("Cannot construct enum Val for type {}", type.name());
        return {};
    }

    return Val::construct(type, [&](void* dest) {
        m_construct(dest, underlying_value);
    });
}

} // namespace fei
