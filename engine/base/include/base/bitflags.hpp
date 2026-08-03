#pragma once
#include <bitset>
#include <ostream>
#include <type_traits>

template<typename T>
class BitFlags {
    using UnderlyingT = std::underlying_type_t<T>;

  public:
    constexpr BitFlags() : m_flags(static_cast<UnderlyingT>(0)) {}
    constexpr BitFlags(T v) : m_flags(to_underlying(v)) {}
    constexpr BitFlags(std::initializer_list<T> vs) : BitFlags() {
        for (T v : vs) {
            m_flags |= to_underlying(v);
        }
    }

    // Checks if a specific flag is set.
    constexpr bool is_set(T v) const {
        return (m_flags & to_underlying(v)) == to_underlying(v);
    }
    // Sets a single flag value.
    constexpr void set(T v) { m_flags |= to_underlying(v); }
    // Unsets a single flag value.
    constexpr void unset(T v) { m_flags &= ~to_underlying(v); }
    // Clears all flag values.
    constexpr void clear() { m_flags = static_cast<UnderlyingT>(0); }

    constexpr operator bool() const {
        return m_flags != static_cast<UnderlyingT>(0);
    }

    friend constexpr BitFlags operator|(BitFlags lhs, T rhs) {
        return BitFlags(lhs.m_flags | to_underlying(rhs));
    }
    friend constexpr BitFlags operator|(BitFlags lhs, BitFlags rhs) {
        return BitFlags(lhs.m_flags | rhs.m_flags);
    }
    friend constexpr BitFlags operator&(BitFlags lhs, T rhs) {
        return BitFlags(lhs.m_flags & to_underlying(rhs));
    }
    friend constexpr BitFlags operator&(BitFlags lhs, BitFlags rhs) {
        return BitFlags(lhs.m_flags & rhs.m_flags);
    }
    friend constexpr BitFlags operator^(BitFlags lhs, T rhs) {
        return BitFlags(lhs.m_flags ^ to_underlying(rhs));
    }
    friend constexpr BitFlags operator^(BitFlags lhs, BitFlags rhs) {
        return BitFlags(lhs.m_flags ^ rhs.m_flags);
    }

    friend constexpr BitFlags& operator|=(BitFlags& lhs, T rhs) {
        lhs.m_flags |= to_underlying(rhs);
        return lhs;
    }
    friend constexpr BitFlags& operator|=(BitFlags& lhs, BitFlags rhs) {
        lhs.m_flags |= rhs.m_flags;
        return lhs;
    }
    friend constexpr BitFlags& operator&=(BitFlags& lhs, T rhs) {
        lhs.m_flags &= to_underlying(rhs);
        return lhs;
    }
    friend constexpr BitFlags& operator&=(BitFlags& lhs, BitFlags rhs) {
        lhs.m_flags &= rhs.m_flags;
        return lhs;
    }
    friend constexpr BitFlags& operator^=(BitFlags& lhs, T rhs) {
        lhs.m_flags ^= to_underlying(rhs);
        return lhs;
    }
    friend constexpr BitFlags& operator^=(BitFlags& lhs, BitFlags rhs) {
        lhs.m_flags ^= rhs.m_flags;
        return lhs;
    }

    friend constexpr BitFlags operator~(const BitFlags& bf) {
        return BitFlags(~bf.m_flags);
    }

    friend constexpr bool operator==(const BitFlags& lhs, const BitFlags& rhs) {
        return lhs.m_flags == rhs.m_flags;
    }
    friend constexpr bool operator!=(const BitFlags& lhs, const BitFlags& rhs) {
        return lhs.m_flags != rhs.m_flags;
    }

    // Stream output operator for debugging.
    friend std::ostream& operator<<(std::ostream& os, const BitFlags& bf) {
        // Write out a bitset representation.
        os << std::bitset<sizeof(UnderlyingT) * 8>(bf.m_flags);
        return os;
    }

    // Construct BitFlags from raw values.
    static constexpr BitFlags from_raw(UnderlyingT flags) {
        return BitFlags(flags);
    }
    // Retrieve the raw underlying flags.
    constexpr UnderlyingT to_raw() const { return m_flags; }

  private:
    constexpr explicit BitFlags(UnderlyingT flags) : m_flags(flags) {}
    static constexpr UnderlyingT to_underlying(T v) {
        return static_cast<UnderlyingT>(v);
    }
    UnderlyingT m_flags;
};
