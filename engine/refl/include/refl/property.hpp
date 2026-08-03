#pragma once
#include "refl/callable.hpp"
#include "refl/conversion.hpp"
#include "refl/ref.hpp"
#include "refl/ref_utils.hpp"
#include "refl/type.hpp"
#include "refl/utils.hpp"

#include <cstddef>
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

inline Status<InvokeFailure> assign_exact_dynamic_property(
    TypeId target_type,
    void* target,
    Ref value,
    const std::string& name
) {
    if (!value || value.type_id() != target_type) {
        return failure(
            InvokeFailure::invalid_call(
                "Invalid value passed to property set " + name + ": expected " +
                type_name(target_type) + ", got " + describe_ref(value)
            )
        );
    }

    auto type = Registry::instance().try_get_type(target_type);
    if (!type) {
        return failure(InvokeFailure::invalid_call(type.error().message));
    }

    if (type->copy_assign(target, value.const_ptr())) {
        return {};
    }
    if (type->destructible() && type->copy_constructible()) {
        type->destroy(target);
        type->copy_construct(target, value.const_ptr());
        return {};
    }

    return failure(
        InvokeFailure::invalid_call("Property " + name + " is not assignable")
    );
}

inline Status<InvokeFailure> assign_dynamic_property(
    TypeId target_type,
    void* target,
    Ref value,
    const std::string& name
) {
    if (target_type == fei::type_id<bool>()) {
        return assign_property(*static_cast<bool*>(target), value, name);
    }
    if (target_type == fei::type_id<int>()) {
        return assign_property(*static_cast<int*>(target), value, name);
    }
    if (target_type == fei::type_id<unsigned int>()) {
        return assign_property(
            *static_cast<unsigned int*>(target),
            value,
            name
        );
    }
    if (target_type == fei::type_id<float>()) {
        return assign_property(*static_cast<float*>(target), value, name);
    }
    if (target_type == fei::type_id<double>()) {
        return assign_property(*static_cast<double*>(target), value, name);
    }
    if (target_type == fei::type_id<std::string>()) {
        return assign_property(*static_cast<std::string*>(target), value, name);
    }
    return assign_exact_dynamic_property(target_type, target, value, name);
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

class OffsetProperty final : public Property {
  private:
    TypeId m_owner_type_id;
    std::size_t m_offset;

  public:
    OffsetProperty(
        std::string name,
        TypeId owner_type_id,
        TypeId type_id,
        std::size_t offset
    ) :
        Property(std::move(name), type_id), m_owner_type_id(owner_type_id),
        m_offset(offset) {}

    Result<Ref, InvokeFailure> get(Ref obj) const override {
        if (!obj || obj.type_id() != m_owner_type_id) {
            return failure(
                InvokeFailure::invalid_call(
                    "Invalid object passed to property get " + name()
                )
            );
        }
        if (obj.is_const()) {
            auto* base = static_cast<const std::byte*>(obj.const_ptr());
            return Ref(base + m_offset, type_id());
        }
        auto* base = static_cast<std::byte*>(obj.ptr());
        return Ref(base + m_offset, type_id());
    }

    Status<InvokeFailure> set(Ref obj, Ref value) const override {
        if (!obj || obj.type_id() != m_owner_type_id || obj.is_const()) {
            return failure(
                InvokeFailure::invalid_call(
                    "Invalid or const object passed to property set " + name()
                )
            );
        }
        auto* base = static_cast<std::byte*>(obj.ptr());
        return detail::assign_dynamic_property(
            type_id(),
            base + m_offset,
            value,
            name()
        );
    }

    std::size_t offset() const { return m_offset; }
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
