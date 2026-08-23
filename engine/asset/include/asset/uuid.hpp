#pragma once

#include "base/result.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace ets {

class AssetUuid {
  public:
    AssetUuid() = default;

    static AssetUuid random();
    static Result<AssetUuid, std::string> parse(std::string_view value);

    [[nodiscard]] bool is_nil() const;
    [[nodiscard]] std::string as_string() const;
    [[nodiscard]] const std::array<std::uint8_t, 16>& bytes() const {
        return m_bytes;
    }

    bool operator==(const AssetUuid&) const = default;

  private:
    explicit AssetUuid(std::array<std::uint8_t, 16> bytes) : m_bytes(bytes) {}

    std::array<std::uint8_t, 16> m_bytes {};
};

} // namespace ets

template<>
struct std::hash<ets::AssetUuid> {
    std::size_t operator()(const ets::AssetUuid& value) const noexcept;
};
