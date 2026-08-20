#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>

namespace fei {

struct Entity {
    std::uint32_t value {};

    constexpr explicit operator std::uint32_t() const { return value; }
    auto operator<=>(const Entity&) const = default;
};

using ArchetypeId = std::uint32_t;
using ScheduleId = std::size_t;
using SystemId = std::uint32_t;

struct ScheduledSystemHandle {
    ScheduleId schedule;
    SystemId id;
};

using SystemHandle = ScheduledSystemHandle;

struct RegisteredSystemId {
    SystemId value;

    bool operator==(const RegisteredSystemId&) const = default;
};

} // namespace fei

namespace std {

template<>
struct hash<fei::Entity> { // NOLINT(readability-identifier-naming)
    size_t operator()(fei::Entity entity) const noexcept {
        return hash<uint32_t> {}(entity.value);
    }
};

template<>
class numeric_limits<fei::Entity> : public numeric_limits<uint32_t> {
  public:
    static constexpr fei::Entity min() noexcept {
        return fei::Entity {numeric_limits<uint32_t>::min()};
    }
    static constexpr fei::Entity lowest() noexcept {
        return fei::Entity {numeric_limits<uint32_t>::lowest()};
    }
    static constexpr fei::Entity max() noexcept {
        return fei::Entity {numeric_limits<uint32_t>::max()};
    }
};

template<>
struct formatter<fei::Entity, char> : formatter<uint32_t, char> {
    template<class FormatContext>
    auto format(fei::Entity entity, FormatContext& context) const {
        return formatter<uint32_t, char>::format(entity.value, context);
    }
};

} // namespace std
