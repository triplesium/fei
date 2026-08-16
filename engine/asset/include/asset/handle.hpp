#pragma once
#include "asset/id.hpp"
#include "base/optional.hpp"

#include <memory>
#include <utility>

namespace fei {

template<typename T>
class Assets;

template<typename T>
class Handle;

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

} // namespace fei
