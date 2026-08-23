#include "asset/uuid.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <random>
#include <string>
#include <system_error>

namespace ets {

AssetUuid AssetUuid::random() {
    std::array<std::uint8_t, 16> bytes {};
    std::random_device random;
    for (auto& byte : bytes) {
        byte = static_cast<std::uint8_t>(random());
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);
    return AssetUuid(bytes);
}

Result<AssetUuid, std::string> AssetUuid::parse(std::string_view value) {
    if (value.size() != 36 || value[8] != '-' || value[13] != '-' ||
        value[18] != '-' || value[23] != '-') {
        return failure(
            std::string("Invalid asset UUID: ") + std::string(value)
        );
    }

    std::array<std::uint8_t, 16> bytes {};
    std::size_t output = 0;
    for (std::size_t index = 0; index < value.size();) {
        if (value[index] == '-') {
            ++index;
            continue;
        }
        unsigned int byte = 0;
        const auto* first = value.data() + index;
        const auto* last = first + 2;
        const auto [end, error] = std::from_chars(first, last, byte, 16);
        if (error != std::errc {} || end != last || output >= bytes.size()) {
            return failure(
                std::string("Invalid asset UUID: ") + std::string(value)
            );
        }
        bytes[output++] = static_cast<std::uint8_t>(byte);
        index += 2;
    }
    if (output != bytes.size()) {
        return failure(
            std::string("Invalid asset UUID: ") + std::string(value)
        );
    }
    return AssetUuid(bytes);
}

bool AssetUuid::is_nil() const {
    return std::ranges::all_of(m_bytes, [](std::uint8_t byte) {
        return byte == 0;
    });
}

std::string AssetUuid::as_string() const {
    return std::format(
        "{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-"
        "{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
        m_bytes[0],
        m_bytes[1],
        m_bytes[2],
        m_bytes[3],
        m_bytes[4],
        m_bytes[5],
        m_bytes[6],
        m_bytes[7],
        m_bytes[8],
        m_bytes[9],
        m_bytes[10],
        m_bytes[11],
        m_bytes[12],
        m_bytes[13],
        m_bytes[14],
        m_bytes[15]
    );
}

} // namespace ets

std::size_t std::hash<ets::AssetUuid>::operator()(
    const ets::AssetUuid& value
) const noexcept {
    std::size_t hash = 1469598103934665603ULL;
    for (const auto byte : value.bytes()) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}
