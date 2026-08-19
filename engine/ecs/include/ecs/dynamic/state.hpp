#pragma once

#include "base/optional.hpp"
#include "base/result.hpp"
#include "ecs/dynamic/system_param.hpp"
#include "ecs/fwd.hpp"
#include "ecs/system_access.hpp"
#include "refl/ref.hpp"
#include "refl/type.hpp"

#include <functional>
#include <shared_mutex>
#include <unordered_map>

namespace fei {

struct DynamicStateOps {
    TypeId value_type;
    TypeId state_resource;
    TypeId next_state_resource;
    std::function<bool(const World&)> initialized;
    std::function<Ref(World&)> current;
    std::function<Status<DynamicSystemError>(World&, Ref)> set_next;
    std::function<void(World&)> clear_next;
    std::function<ScheduleId(Ref)> on_enter;
    std::function<ScheduleId(Ref)> on_exit;
    std::function<ScheduleId(Ref, Ref)> on_transition;
};

class DynamicStateRegistry {
  private:
    mutable std::shared_mutex m_mutex;
    std::unordered_map<TypeId, DynamicStateOps> m_states;

  public:
    static DynamicStateRegistry& instance();

    void add(DynamicStateOps ops);
    Optional<DynamicStateOps> find(TypeId value_type) const;
};

Result<DynamicStateOps, DynamicSystemError>
resolve_dynamic_state(TypeId value_type);

class DynamicStateRef {
  private:
    World* m_world {nullptr};
    DynamicStateOps m_ops;

  public:
    DynamicStateRef() = default;
    DynamicStateRef(World& world, DynamicStateOps ops) :
        m_world(&world), m_ops(ops) {}

    TypeId value_type() const { return m_ops.value_type; }
    Ref get() const;
};

class DynamicNextStateRef {
  private:
    World* m_world {nullptr};
    DynamicStateOps m_ops;

  public:
    DynamicNextStateRef() = default;
    DynamicNextStateRef(World& world, DynamicStateOps ops) :
        m_world(&world), m_ops(ops) {}

    TypeId value_type() const { return m_ops.value_type; }
    Status<DynamicSystemError> set(Ref value) const;
    void clear() const;
};

class DynamicStateParam final : public DynamicSystemParam {
  private:
    DynamicStateOps m_ops;
    DynamicStateRef m_ref;

  public:
    explicit DynamicStateParam(DynamicStateOps ops) : m_ops(ops) {}

    SystemAccess access() const override;
    Result<Ref, DynamicSystemError>
    prepare(World& world, SystemTicks system_ticks) override;
};

class DynamicNextStateParam final : public DynamicSystemParam {
  private:
    DynamicStateOps m_ops;
    DynamicNextStateRef m_ref;

  public:
    explicit DynamicNextStateParam(DynamicStateOps ops) : m_ops(ops) {}

    SystemAccess access() const override;
    Result<Ref, DynamicSystemError>
    prepare(World& world, SystemTicks system_ticks) override;
};

} // namespace fei
