#pragma once
#include "refl/ref.hpp"
#include "refl/ref_utils.hpp"
#include "refl/type.hpp"
#include "refl/utils.hpp"

#include <string>
#include <type_traits>

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
    virtual void set(Ref obj, Ref value) const = 0;
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
            auto& p = obj.get<ParentType>();
            return make_ref(p.*m_ptr);
        }
    }

    void set(Ref obj, Ref value) const override {
        if constexpr (is_static) {
            if constexpr (std::is_array_v<MemberType>) {
                auto& src = value.get<MemberType>();
                std::memcpy(*m_ptr, src, sizeof(MemberType));
            } else {
                if constexpr (std::is_move_assignable_v<MemberType>) {
                    *m_ptr = std::move(value.get<MemberType>());
                } else {
                    // For non-assignable types, reconstruct in place
                    *m_ptr = MemberType(std::move(value.get<MemberType>()));
                }
            }
        } else {
            auto& p = obj.get<ParentType>();
            if constexpr (std::is_array_v<MemberType>) {
                auto& src = value.get<MemberType>();
                std::memcpy(&(p.*m_ptr), &src, sizeof(MemberType));
            } else {
                if constexpr (std::is_move_assignable_v<MemberType>) {
                    p.*m_ptr = std::move(value.get<MemberType>());
                } else {
                    // For non-assignable types, reconstruct in place
                    p.*m_ptr = MemberType(std::move(value.get<MemberType>()));
                }
            }
        }
    }
};

} // namespace fei
