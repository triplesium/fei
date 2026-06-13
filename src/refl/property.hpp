#pragma once
#include "base/log.hpp"
#include "refl/ref.hpp"
#include "refl/ref_utils.hpp"
#include "refl/type.hpp"
#include "refl/utils.hpp"

#include <cstring>
#include <string>
#include <type_traits>
#include <utility>

namespace fei {

class Property {
  private:
    std::string m_name;
    TypeId m_type_id;

  public:
    Property(std::string name, TypeId type_id) :
        m_name(std::move(name)), m_type_id(type_id) {}
    virtual ~Property() = default;

    virtual Ref get(Ref obj) const = 0;
    virtual bool set(Ref obj, Ref value) const = 0;
    const std::string& name() const { return m_name; }
    TypeId type_id() const { return m_type_id; }
};

template<typename P>
class PropertyImpl : public Property {
  private:
    using MemberType = typename MemberTrait<P>::Type;
    using ParentType = typename MemberTrait<P>::ParentType;
    constexpr static bool is_static = MemberTrait<P>::is_static;
    P m_ptr;

  public:
    PropertyImpl(std::string name, P ptr) :
        Property(std::move(name), fei::type_id<MemberType>()), m_ptr(ptr) {}

    Ref get(Ref obj) const override {
        if constexpr (is_static) {
            return make_ref(*m_ptr);
        } else {
            if (auto* p = obj.try_get<ParentType>()) {
                return make_ref(p->*m_ptr);
            }
            if (auto* p = obj.try_get_const<ParentType>()) {
                return make_ref(p->*m_ptr);
            }
            error("Invalid object passed to property get {}", name());
            return {};
        }
    }

    bool set(Ref obj, Ref value) const override {
        if (!value || value.type_id() != fei::type_id<MemberType>()) {
            error("Invalid value passed to property set {}", name());
            return false;
        }
        if constexpr (is_static) {
            if constexpr (std::is_array_v<MemberType>) {
                auto* src = value.try_get_const<MemberType>();
                if (!src) {
                    return false;
                }
                std::memcpy(*m_ptr, *src, sizeof(MemberType));
                return true;
            } else {
                if constexpr (std::is_copy_assignable_v<MemberType>) {
                    *m_ptr = value.get_const<MemberType>();
                    return true;
                } else if constexpr (std::is_move_assignable_v<MemberType>) {
                    if (value.is_const()) {
                        error(
                            "Cannot move-assign property {} from const value",
                            name()
                        );
                        return false;
                    }
                    *m_ptr = std::move(value.get<MemberType>());
                    return true;
                } else {
                    error("Property {} is not assignable", name());
                    return false;
                }
            }
        } else {
            auto* p = obj.try_get<ParentType>();
            if (!p) {
                error(
                    "Invalid or const object passed to property set {}",
                    name()
                );
                return false;
            }
            if constexpr (std::is_array_v<MemberType>) {
                auto* src = value.try_get_const<MemberType>();
                if (!src) {
                    return false;
                }
                std::memcpy(&(p->*m_ptr), src, sizeof(MemberType));
                return true;
            } else {
                if constexpr (std::is_copy_assignable_v<MemberType>) {
                    p->*m_ptr = value.get_const<MemberType>();
                    return true;
                } else if constexpr (std::is_move_assignable_v<MemberType>) {
                    if (value.is_const()) {
                        error(
                            "Cannot move-assign property {} from const value",
                            name()
                        );
                        return false;
                    }
                    p->*m_ptr = std::move(value.get<MemberType>());
                    return true;
                } else {
                    error("Property {} is not assignable", name());
                    return false;
                }
            }
        }
    }
};

} // namespace fei
