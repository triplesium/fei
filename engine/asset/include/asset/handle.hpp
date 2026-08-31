#pragma once
#include "asset/id.hpp"
#include "base/optional.hpp"
#include "refl/argument_adapter.hpp"
#include "refl/ref.hpp"

#include <memory>
#include <string>
#include <utility>

namespace ets {

template<typename T>
class Assets;

template<typename T>
class Handle;

class UntypedHandle;
using AssetHandleConverter = UntypedHandle (*)(Ref);

struct AssetHandleState {
    AssetId id {invalid_asset_id};
};

class UntypedHandle {
  private:
    TypeId m_asset_type;
    std::shared_ptr<AssetHandleState> m_state;

    template<typename T>
    explicit UntypedHandle(const Handle<T>& handle) :
        m_asset_type(type_id<T>()), m_state(handle.m_state) {}

    template<typename T>
    explicit UntypedHandle(Handle<T>&& handle) :
        m_asset_type(type_id<T>()), m_state(std::move(handle.m_state)) {}

  public:
    UntypedHandle() = default;

    TypeId asset_type() const { return m_asset_type; }
    AssetId id() const { return m_state ? m_state->id : invalid_asset_id; }

    bool is_valid() const { return m_state != nullptr; }
    explicit operator bool() const { return is_valid(); }

    template<typename T>
    bool is() const {
        return m_asset_type == type_id<T>();
    }

    template<typename T>
    Optional<Handle<T>> try_typed() const;

    template<typename T>
    friend class Handle;
};

template<typename T>
class Handle {
  private:
    std::shared_ptr<AssetHandleState> m_state;

    explicit Handle(std::shared_ptr<AssetHandleState> state) :
        m_state(std::move(state)) {}

  public:
    Handle() = default;

    AssetId id() const { return m_state ? m_state->id : invalid_asset_id; }

    bool is_valid() const { return m_state != nullptr; }
    explicit operator bool() const { return is_valid(); }

    UntypedHandle untyped() const { return UntypedHandle(*this); }

    friend class Assets<T>;
    friend class UntypedHandle;
};

template<typename T>
Optional<Handle<T>> UntypedHandle::try_typed() const {
    if (!is<T>()) {
        return nullopt;
    }
    return Handle<T>(m_state);
}

void register_asset_handle_converter(
    TypeId handle_type,
    AssetHandleConverter converter
);

[[nodiscard]] Optional<UntypedHandle> convert_to_untyped_handle(Ref handle);

template<typename T>
struct Conversion<Handle<T>> {
    static ConversionRank match(const Ref& ref) {
        if (!ref) {
            return ConversionRank::None;
        }
        if (ref.type_id() == type_id<Handle<T>>()) {
            return ConversionRank::Exact;
        }
        const auto* handle = ref.try_get_const<UntypedHandle>();
        return handle != nullptr && handle->template is<T>() ?
                   ConversionRank::Weak :
                   ConversionRank::None;
    }

    static Handle<T> get(const Ref& ref) {
        if (ref.type_id() == type_id<Handle<T>>()) {
            return ref.get_const<Handle<T>>();
        }
        return *ref.get_const<UntypedHandle>().template try_typed<T>();
    }
};

template<typename T>
struct Conversion<Optional<Handle<T>>> {
    static ConversionRank match(const Ref& ref) {
        if (!ref) {
            return ConversionRank::None;
        }
        if (ref.type_id() == type_id<Optional<Handle<T>>>()) {
            return ConversionRank::Exact;
        }
        return Conversion<Handle<T>>::match(ref) != ConversionRank::None ?
                   ConversionRank::Weak :
                   ConversionRank::None;
    }

    static Optional<Handle<T>> get(const Ref& ref) {
        if (ref.type_id() == type_id<Optional<Handle<T>>>()) {
            return ref.get_const<Optional<Handle<T>>>();
        }
        return Conversion<Handle<T>>::get(ref);
    }
};

template<typename T>
struct ArgumentAdapter<const Handle<T>&> {
    static ConversionRank match(const Ref& ref) {
        return Conversion<Handle<T>>::match(ref);
    }

    static bool accepts(const Ref& ref) {
        return match(ref) != ConversionRank::None;
    }

    static Handle<T> get(const Ref& ref) {
        return Conversion<Handle<T>>::get(ref);
    }

    static std::string expected_type() {
        return std::string(type_name<Handle<T>>());
    }

    static std::string actual_type(const Ref& ref) {
        return detail::describe_ref(ref);
    }

    static std::string describe_mismatch(const Ref& ref) {
        return "expected " + expected_type() + ", got " + actual_type(ref);
    }
};

template<>
struct ArgumentAdapter<const UntypedHandle&> {
    static ConversionRank match(const Ref& ref) {
        if (!ref) {
            return ConversionRank::None;
        }
        if (ref.type_id() == type_id<UntypedHandle>()) {
            return ConversionRank::Exact;
        }
        return convert_to_untyped_handle(ref) ? ConversionRank::Weak :
                                                ConversionRank::None;
    }

    static bool accepts(const Ref& ref) {
        return match(ref) != ConversionRank::None;
    }

    static UntypedHandle get(const Ref& ref) {
        if (ref.type_id() == type_id<UntypedHandle>()) {
            return ref.get_const<UntypedHandle>();
        }
        auto converted = convert_to_untyped_handle(ref);
        ETS_ASSERT(converted);
        return std::move(*converted);
    }

    static std::string expected_type() {
        return std::string(type_name<UntypedHandle>());
    }

    static std::string actual_type(const Ref& ref) {
        return detail::describe_ref(ref);
    }

    static std::string describe_mismatch(const Ref& ref) {
        return "expected " + expected_type() + ", got " + actual_type(ref);
    }
};

template<typename T>
void register_asset_handle_converter() {
    register_asset_handle_converter(type_id<Handle<T>>(), [](Ref handle) {
        return handle.get_const<Handle<T>>().untyped();
    });
}

} // namespace ets
