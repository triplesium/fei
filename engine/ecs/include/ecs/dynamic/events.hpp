#pragma once

#include "base/optional.hpp"
#include "ecs/dynamic/system_param.hpp"
#include "refl/type.hpp"
#include "refl/val.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace fei {

class DynamicEvents {
  public:
    struct Sequence {
        std::vector<Val> events;
        std::size_t start_event_count {};
    };

    struct Channel {
        Sequence previous;
        Sequence current;
        std::size_t event_count {};
    };

    void send(TypeId type, Val event);
    void update();
    [[nodiscard]] const Channel* channel(TypeId type) const;
    [[nodiscard]] Channel* channel(TypeId type);
    [[nodiscard]] std::size_t oldest_event_count(TypeId type) const;
    Optional<Ref> get(TypeId type, std::size_t event_id, bool read_only);

    const std::unordered_map<TypeId, Channel>& channels() const {
        return m_channels;
    }
    void set_channel(TypeId type, Channel channel);

  private:
    std::unordered_map<TypeId, Channel> m_channels;
};

enum class DynamicEventParamKind {
    Writer,
    Reader,
    ReaderRO,
};

class DynamicEventParam final : public DynamicSystemParam {
  private:
    TypeId m_event_type;
    DynamicEventParamKind m_kind;
    bool m_optional {false};
    DynamicEvents* m_events {nullptr};
    std::size_t m_cursor {};
    bool m_initialized {false};

  public:
    std::string name;

    DynamicEventParam(
        std::string name,
        TypeId event_type,
        DynamicEventParamKind kind,
        bool optional = false
    );

    SystemAccess access() const override;
    Result<Ref, DynamicSystemError>
    prepare(World& world, SystemTicks system_ticks) override;

    DynamicEventParamKind kind() const { return m_kind; }
    TypeId event_type() const { return m_event_type; }
    Status<DynamicSystemError> send(Val event);
    Optional<Ref> next();
    void reset();

    Result<SystemParamRuntimeState, RuntimeStateError>
    capture_runtime_state() const override;
    Status<RuntimeStateError>
    validate_runtime_state(const SystemParamRuntimeState& state) const override;
    Status<RuntimeStateError>
    restore_runtime_state(const SystemParamRuntimeState& state) override;
    std::uint64_t runtime_state_type() const override;
};

} // namespace fei
