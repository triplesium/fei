#pragma once

#include <cstddef>
#include <cstdint>

namespace fei {

using Entity = std::uint32_t;
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
