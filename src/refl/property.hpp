#pragma once
#include "refl/callable.hpp"
#include "refl/conversion.hpp"
#include "refl/ref.hpp"
#include "refl/ref_utils.hpp"
#include "refl/type.hpp"
#include "refl/utils.hpp"

#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace fei {

namespace detail {

template<class MemberType>
ConversionRank property_conversion_rank(const Ref& value) {
    using ValueType = std::remove_cv_t<MemberType>;
    auto rank = Conversion<ValueType>::match(value);
    if constexpr (std::same_as<ValueType, std::string_view>) {
        return rank == ConversionRank::Exact ? rank : ConversionRank::None;
    } else {
        return rank;
    }
}

template<class MemberType>
Status<InvokeFailure>
assign_array_property(MemberType& target, Ref value, const std::string& name) {
    if (!value || value.type_id() != fei::type_id<MemberType>()) {
        return failure(
            InvokeFailure::invalid_call(
                "Invalid value passed to property set " + name + ": expected " +
                std::string(type_name<MemberType>()) + ", got " +
                describe_ref(value)
            )
        );
    }
    auto* src = value.try_get_const<MemberType>();
    if (!src) {
        return failure(
            InvokeFailure::invalid_call(
                "Invalid value passed to property set " + name + ": expected " +
                std::string(type_name<MemberType>()) + ", got " +
                describe_ref(value)
            )
        );
    }
    std::memcpy(&target, src, sizeof(MemberType));
    return {};
}

template<class MemberType>
Status<InvokeFailure>
assign_property(MemberType& target, Ref value, const std::string& name) {
    using ValueType = std::remove_cv_t<MemberType>;

    if constexpr (std::is_array_v<MemberType>) {
        return assign_array_property(target, value, name);
    } else if constexpr (std::is_copy_assignable_v<MemberType>) {
        auto rank = property_conversion_rank<ValueType>(value);
        if (rank == ConversionRank::None) {
            return failure(
                InvokeFailure::invalid_call(
                    "Invalid value passed to property set " + name +
                    ": expected " + std::string(type_name<ValueType>()) +
                    ", got " + describe_ref(value)
                )
            );
        }
        if (rank == ConversionRank::Exact) {
            target = value.get_const<ValueType>();
        } else {
            static_assert(
                std::is_copy_constructible_v<ValueType>,
                "Weak property conversion requires a copy-constructible type"
            );
            target = Conversion<ValueType>::get(value);
        }
        return {};
    } else if constexpr (std::is_move_assignable_v<MemberType>) {
        if (!value || value.type_id() != fei::type_id<ValueType>()) {
            return failure(
                InvokeFailure::invalid_call(
                    "Invalid value passed to property set " + name +
                    ": expected " + std::string(type_name<ValueType>()) +
                    ", got " + describe_ref(value)
                )
            );
        }
        if (value.is_const()) {
            return failure(
                InvokeFailure::invalid_call(
                    "Cannot move-assign property " + name + " from const value"
                )
            );
        }
        target = std::move(value.get<ValueType>());
        return {};
    } else {
        return failure(
            InvokeFailure::invalid_call(
                "Property " + name + " is not assignable"
            )
        );
    }
}

} // namespace detail

class Property {
  private:
    std::string m_name;
    TypeId m_type_id;

  public:
    Property(std::string name, TypeId type_id) :
        m_name(std::move(name)), m_type_id(type_id) {}
    virtual ~Property() = default;

    virtual Result<Ref, InvokeFailure> get(Ref obj) const = 0;
    virtual Status<InvokeFailure> set(Ref obj, Ref value) const = 0;
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

    Result<Ref, InvokeFailure> get(Ref obj) const override {
        if constexpr (is_static) {
            return make_ref(*m_ptr);
        } else {
            if (auto* p = obj.try_get<ParentType>()) {
                return make_ref(p->*m_ptr);
            }
            if (auto* p = obj.try_get_const<ParentType>()) {
                return make_ref(p->*m_ptr);
            }
            return failure(
                InvokeFailure::invalid_call(
                    "Invalid object passed to property get " + name()
                )
            );
        }
    }

    Status<InvokeFailure> set(Ref obj, Ref value) const override {
        if constexpr (is_static) {
            return detail::assign_property(*m_ptr, value, name());
        } else {
            auto* p = obj.try_get<ParentType>();
            if (!p) {
                return failure(
                    InvokeFailure::invalid_call(
                        "Invalid or const object passed to property set " +
                        name()
                    )
                );
            }
            return detail::assign_property(p->*m_ptr, value, name());
        }
    }
};

} // namespace fei
