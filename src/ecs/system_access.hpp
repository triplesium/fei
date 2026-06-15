#pragma once

#include "base/concepts.hpp"
#include "ecs/fwd.hpp"
#include "refl/type.hpp"

#include <tuple>
#include <type_traits>
#include <unordered_set>

namespace fei {

template<typename T>
class Res;

template<typename T>
class CRes;

class WorldRef;
class Commands;
struct CommandsQueue;

template<typename T>
class Events;

template<typename T>
class EventReader;

template<typename T>
class EventWriter;

template<typename... Datas>
class Query;

template<typename Q, typename... Filters>
    requires SpecializationOf<Q, Query>
class FilteredQuery;

struct SystemAccess {
    std::unordered_set<TypeId> read_resources;
    std::unordered_set<TypeId> write_resources;
    std::unordered_set<TypeId> read_components;
    std::unordered_set<TypeId> write_components;
    bool world_exclusive {false};
    bool commands {false};

    bool conflicts_with(const SystemAccess& other) const {
        if (world_exclusive || other.world_exclusive || commands ||
            other.commands) {
            return true;
        }

        return intersects(write_resources, other.write_resources) ||
               intersects(write_resources, other.read_resources) ||
               intersects(read_resources, other.write_resources) ||
               intersects(write_components, other.write_components) ||
               intersects(write_components, other.read_components) ||
               intersects(read_components, other.write_components);
    }

    bool is_barrier() const { return world_exclusive || commands; }

  private:
    static bool intersects(
        const std::unordered_set<TypeId>& lhs,
        const std::unordered_set<TypeId>& rhs
    ) {
        if (lhs.size() > rhs.size()) {
            return intersects(rhs, lhs);
        }
        for (auto type : lhs) {
            if (rhs.contains(type)) {
                return true;
            }
        }
        return false;
    }
};

namespace detail {

template<typename T>
struct SystemParamAccess {
    static void add(SystemAccess& access) { access.world_exclusive = true; }
};

template<typename T>
struct SystemParamAccess<Res<T>> {
    static void add(SystemAccess& access) {
        access.write_resources.insert(type_id<T>());
    }
};

template<typename T>
struct SystemParamAccess<CRes<T>> {
    static void add(SystemAccess& access) {
        access.read_resources.insert(type_id<T>());
    }
};

template<>
struct SystemParamAccess<WorldRef> {
    static void add(SystemAccess& access) { access.world_exclusive = true; }
};

template<>
struct SystemParamAccess<Commands> {
    static void add(SystemAccess& access) {
        access.commands = true;
        access.write_resources.insert(type_id<CommandsQueue>());
    }
};

template<typename T>
struct SystemParamAccess<EventReader<T>> {
    static void add(SystemAccess& access) {
        access.write_resources.insert(type_id<Events<T>>());
    }
};

template<typename T>
struct SystemParamAccess<EventWriter<T>> {
    static void add(SystemAccess& access) {
        access.write_resources.insert(type_id<Events<T>>());
    }
};

template<typename T>
void add_query_data_access(SystemAccess& access) {
    using U = std::remove_reference_t<T>;
    using Component = std::remove_cv_t<U>;
    if constexpr (std::same_as<Component, Entity>) {
        return;
    } else if constexpr (std::is_const_v<U>) {
        access.read_components.insert(type_id<Component>());
    } else {
        access.write_components.insert(type_id<Component>());
    }
}

template<typename... Datas>
struct SystemParamAccess<Query<Datas...>> {
    static void add(SystemAccess& access) {
        (add_query_data_access<Datas>(access), ...);
    }
};

template<typename Q, typename... Filters>
    requires SpecializationOf<Q, Query>
struct SystemParamAccess<FilteredQuery<Q, Filters...>> {
    static void add(SystemAccess& access) { SystemParamAccess<Q>::add(access); }
};

template<typename Tuple>
struct SystemAccessBuilder;

template<typename... Params>
struct SystemAccessBuilder<std::tuple<Params...>> {
    static SystemAccess build() {
        SystemAccess access;
        (SystemParamAccess<Params>::add(access), ...);
        return access;
    }
};

} // namespace detail

template<typename ParamTuple>
SystemAccess system_access_for_params() {
    return detail::SystemAccessBuilder<ParamTuple>::build();
}

} // namespace fei
