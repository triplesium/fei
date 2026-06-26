#pragma once

#include <cstddef>
#include <cstdint>

namespace fei {

using Entity = std::uint32_t;
using ArchetypeId = std::uint32_t;
using ScheduleId = std::size_t;
using SystemId = std::uint32_t;

struct SystemHandle {
    ScheduleId schedule;
    SystemId id;
};

} // namespace fei
