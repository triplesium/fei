#pragma once
#include "asset/id.hpp"

#include <memory>

namespace fei {

template<typename T>
class Assets;

template<typename T>
struct AssetsState;

template<typename T>
class Handle {
  private:
    AssetId m_id;
    std::shared_ptr<AssetsState<T>> m_state;

    Assets<T>* get_assets() const {
        return m_state ? m_state->assets : nullptr;
    }

  public:
    Handle() : m_id(0), m_state(nullptr) {}
    Handle(AssetId id, std::shared_ptr<AssetsState<T>> state);
    ~Handle();

    Handle(const Handle&);
    Handle& operator=(const Handle&);
    Handle(Handle&&) noexcept;
    Handle& operator=(Handle&&) noexcept;

    AssetId id() const { return m_id; }
};

} // namespace fei

#include "asset/assets.hpp"

namespace fei {

template<typename T>
Handle<T>::Handle(AssetId id, std::shared_ptr<AssetsState<T>> state) :
    m_id(id), m_state(std::move(state)) {
    if (auto* assets = get_assets()) {
        assets->acquire(m_id);
    }
}

template<typename T>
Handle<T>::~Handle() {
    if (auto* assets = get_assets()) {
        assets->release(m_id);
    }
}

template<typename T>
Handle<T>::Handle(const Handle& other) :
    m_id(other.m_id), m_state(other.m_state) {
    if (auto* assets = get_assets()) {
        assets->acquire(m_id);
    }
}

template<typename T>
Handle<T>& Handle<T>::operator=(const Handle& other) {
    if (this != &other) {
        if (auto* assets = get_assets()) {
            assets->release(m_id);
        }
        m_id = other.m_id;
        m_state = other.m_state;
        if (auto* assets = get_assets()) {
            assets->acquire(m_id);
        }
    }
    return *this;
}

template<typename T>
Handle<T>::Handle(Handle&& other) noexcept :
    m_id(other.m_id), m_state(std::move(other.m_state)) {
    other.m_id = 0;
}
template<typename T>
Handle<T>& Handle<T>::operator=(Handle&& other) noexcept {
    if (this != &other) {
        if (auto* assets = get_assets()) {
            assets->release(m_id);
        }
        m_id = other.m_id;
        m_state = std::move(other.m_state);
        other.m_id = 0;
    }
    return *this;
}

} // namespace fei
