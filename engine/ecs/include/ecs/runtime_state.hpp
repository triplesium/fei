#pragma once

#include "ecs/change_detection.hpp"
#include "ecs/fwd.hpp"
#include "ecs/removal_detection.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace fei {

enum class SystemParamRuntimeStateKind : std::uint8_t {
    Uninitialized,
    Stateless,
    Counter,
    Deferred,
};

struct SystemParamRuntimeState {
    SystemParamRuntimeStateKind kind {
        SystemParamRuntimeStateKind::Uninitialized
    };
    std::uint64_t value {};
    std::uint64_t parameter_type {};
};

enum class SystemExecutorRuntimeStateKind : std::uint8_t {
    Stateless,
    Value,
};

class RuntimeStateValue {
  private:
    const void* m_type {nullptr};
    std::shared_ptr<const void> m_value;
    std::size_t m_byte_size {};

    template<typename T>
    static const void* type_key() {
        static const std::uint8_t key {};
        return &key;
    }

  public:
    RuntimeStateValue() = default;

    template<typename T>
        requires std::copy_constructible<std::remove_cvref_t<T>>
    static RuntimeStateValue make(T&& value) {
        using U = std::remove_cvref_t<T>;
        RuntimeStateValue result;
        result.m_type = type_key<U>();
        result.m_value = std::make_shared<const U>(std::forward<T>(value));
        result.m_byte_size = sizeof(U);
        return result;
    }

    template<typename T>
    const std::remove_cvref_t<T>* try_get() const {
        using U = std::remove_cvref_t<T>;
        if (m_type != type_key<U>()) {
            return nullptr;
        }
        return static_cast<const U*>(m_value.get());
    }

    bool has_value() const { return m_value != nullptr; }
    std::size_t byte_size() const { return m_byte_size; }
};

struct SystemExecutorRuntimeState {
    SystemExecutorRuntimeStateKind kind {
        SystemExecutorRuntimeStateKind::Stateless
    };
    RuntimeStateValue value;

    static SystemExecutorRuntimeState stateless() { return {}; }

    template<typename T>
        requires std::copy_constructible<std::remove_cvref_t<T>>
    static SystemExecutorRuntimeState from_value(T&& value) {
        return SystemExecutorRuntimeState {
            .kind = SystemExecutorRuntimeStateKind::Value,
            .value = RuntimeStateValue::make(std::forward<T>(value)),
        };
    }

    std::size_t byte_size() const { return value.byte_size(); }
};

struct SystemRuntimeState {
    Tick last_run {};
    SystemExecutorRuntimeState executor;
    std::vector<SystemParamRuntimeState> params;
};

struct ScheduledSystemRuntimeState {
    SystemId id {};
    SystemRuntimeState system;
    std::vector<SystemRuntimeState> conditions;
};

struct ScheduleRuntimeState {
    ScheduleId id {};
    std::vector<ScheduledSystemRuntimeState> systems;
};

struct SchedulesRuntimeState {
    std::uint64_t topology_generation {};
    std::vector<ScheduleRuntimeState> schedules;
};

struct RegisteredSystemRuntimeState {
    SystemId id {};
    SystemRuntimeState system;
};

struct WorldRuntimeState {
    Tick change_tick {};
    SchedulesRuntimeState schedules;
    std::uint64_t registered_system_generation {};
    std::vector<RegisteredSystemRuntimeState> registered_systems;
    RemovedComponentEvents removed_components;
};

struct RuntimeStateError {
    std::string path;
    std::string message;
};

} // namespace fei
