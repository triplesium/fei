#pragma once

#include "base/concepts.hpp"
#include "base/optional.hpp"
#include "ecs/fwd.hpp"
#include "ecs/resource_traits.hpp"
#include "refl/type.hpp"

#include <tuple>
#include <type_traits>
#include <unordered_set>

namespace fei {

template<typename T>
class ResRO;

template<typename T>
class ResRW;

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
    bool main_thread_only {false};
    bool commands {false};

    void merge(const SystemAccess& other) {
        read_resources.insert(
            other.read_resources.begin(),
            other.read_resources.end()
        );
        write_resources.insert(
            other.write_resources.begin(),
            other.write_resources.end()
        );
        read_components.insert(
            other.read_components.begin(),
            other.read_components.end()
        );
        write_components.insert(
            other.write_components.begin(),
            other.write_components.end()
        );
        world_exclusive = world_exclusive || other.world_exclusive;
        main_thread_only = main_thread_only || other.main_thread_only;
        commands = commands || other.commands;
    }

    bool conflicts_with(const SystemAccess& other) const {
        if (world_exclusive || other.world_exclusive || main_thread_only ||
            other.main_thread_only || commands || other.commands) {
            return true;
        }

        return intersects(write_resources, other.write_resources) ||
               intersects(write_resources, other.read_resources) ||
               intersects(read_resources, other.write_resources) ||
               intersects(write_components, other.write_components) ||
               intersects(write_components, other.read_components) ||
               intersects(read_components, other.write_components);
    }

    bool is_barrier() const {
        return world_exclusive || main_thread_only || commands;
    }

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
struct SystemParamAccess<ResRW<T>> {
    static void add(SystemAccess& access) {
        access.write_resources.insert(type_id<T>());
        if constexpr (ResourceTraits<T>::main_thread_only) {
            access.main_thread_only = true;
        }
    }
};

template<typename T>
struct SystemParamAccess<ResRO<T>> {
    static void add(SystemAccess& access) {
        access.read_resources.insert(type_id<T>());
        if constexpr (ResourceTraits<T>::main_thread_only) {
            access.main_thread_only = true;
        }
    }
};

template<typename T>
struct SystemParamAccess<Optional<ResRW<T>>> {
    static void add(SystemAccess& access) {
        access.write_resources.insert(type_id<T>());
        if constexpr (ResourceTraits<T>::main_thread_only) {
            access.main_thread_only = true;
        }
    }
};

template<typename T>
struct SystemParamAccess<Optional<ResRO<T>>> {
    static void add(SystemAccess& access) {
        access.read_resources.insert(type_id<T>());
        if constexpr (ResourceTraits<T>::main_thread_only) {
            access.main_thread_only = true;
        }
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
