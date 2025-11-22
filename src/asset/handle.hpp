#pragma once
#include "asset/id.hpp"

namespace fei {

template<typename T>
class Assets;

template<typename T>
class Handle {
  private:
    AssetId m_id;
    Assets<T>* m_assets;

  public:
    Handle() : m_id(0), m_assets(nullptr) {}
    Handle(AssetId id, Assets<T>* assets);
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
Handle<T>::Handle(AssetId id, Assets<T>* assets) : m_id(id), m_assets(assets) {
    if (m_assets) {
        m_assets->acquire(m_id);
    }
}

template<typename T>
Handle<T>::~Handle() {
    if (m_assets) {
        m_assets->release(m_id);
    }
}

template<typename T>
Handle<T>::Handle(const Handle& other) :
    m_id(other.m_id), m_assets(other.m_assets) {
    if (m_assets) {
        m_assets->acquire(m_id);
    }
}

template<typename T>
Handle<T>& Handle<T>::operator=(const Handle& other) {
    if (this != &other) {
        if (m_assets) {
            m_assets->release(m_id);
        }
        m_id = other.m_id;
        m_assets = other.m_assets;
        if (m_assets) {
            m_assets->acquire(m_id);
        }
    }
    return *this;
}

template<typename T>
Handle<T>::Handle(Handle&& other) noexcept :
    m_id(other.m_id), m_assets(other.m_assets) {
    other.m_assets = nullptr;
    other.m_id = 0;
}
template<typename T>
Handle<T>& Handle<T>::operator=(Handle&& other) noexcept {
    if (this != &other) {
        if (m_assets) {
            m_assets->release(m_id);
        }
        m_id = other.m_id;
        m_assets = other.m_assets;
        other.m_assets = nullptr;
        other.m_id = 0;
    }
    return *this;
}

} // namespace fei
