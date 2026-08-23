#pragma once

#include "base/log.hpp"
#include "ecs/world.hpp"

#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace ets {

struct ExtractMainWorld {
    World* world {nullptr};
    std::uint64_t source_revision {0};
};

template<typename P>
concept ReadOnlySystemParam =
    SystemParam<P> && IsReadOnlySystemParam<std::remove_cvref_t<P>>::value;

template<ReadOnlySystemParam P>
class Extract {
  public:
    explicit Extract(P param) : m_param(std::move(param)) {}

    P& get() { return m_param; }
    const P& get() const { return m_param; }

    P& operator*() { return m_param; }
    const P& operator*() const { return m_param; }
    P* operator->() { return &m_param; }
    const P* operator->() const { return &m_param; }

  private:
    P m_param;
};

template<ReadOnlySystemParam P>
struct SystemParamTraits<Extract<P>> {
    struct State {
        std::optional<typename SystemParamTraits<P>::State> inner;
        std::uint64_t source_revision {0};
        Tick last_run {0};
    };

    static State init_state(World& render_world) {
        const auto& source = static_cast<const World&>(render_world)
                                 .resource<ExtractMainWorld>();
        if (source.world == nullptr) {
            fatal("Extract system params are only available in RenderExtract");
        }
        return State {
            .inner =
                std::optional<typename SystemParamTraits<P>::State> {
                    std::in_place,
                    SystemParamTraits<P>::init_state(*source.world),
                },
            .source_revision = source.source_revision,
        };
    }

    static Extract<P>
    get_param(World& render_world, State& state, SystemTicks) {
        const auto& source = static_cast<const World&>(render_world)
                                 .resource<ExtractMainWorld>();
        if (source.world == nullptr) {
            fatal("Extract system params are only available in RenderExtract");
        }

        if (!state.inner || state.source_revision != source.source_revision) {
            state.inner.reset();
            state.inner.emplace(
                SystemParamTraits<P>::init_state(*source.world)
            );
            state.source_revision = source.source_revision;
            state.last_run = 0;
        }

        auto& main_world = *source.world;
        const SystemTicks main_ticks {
            .last_run = state.last_run,
            .this_run = main_world.read_change_tick(),
        };
        auto param = SystemParamTraits<P>::get_param(
            main_world,
            *state.inner,
            main_ticks
        );
        state.last_run = main_ticks.this_run;
        return Extract<P> {std::move(param)};
    }
};

namespace detail {

template<ReadOnlySystemParam P>
struct SystemParamAccess<Extract<P>> {
    static void add(SystemAccess& access) {
        SystemAccess source_access;
        SystemParamAccess<P>::add(source_access);
        access.main_thread_only =
            access.main_thread_only || source_access.main_thread_only;
    }
};

} // namespace detail

} // namespace ets
