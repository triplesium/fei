#pragma once
#include "asset/id.hpp"

#include <memory>
#include <utility>

namespace fei {

template<typename T>
class Assets;

struct AssetHandleState {
    AssetId id {invalid_asset_id};
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

    friend class Assets<T>;
};

} // namespace fei
