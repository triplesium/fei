#pragma once
#include "base/concepts.hpp"
#include "ecs/archetype.hpp"
#include "ecs/change_detection.hpp"
#include "ecs/fwd.hpp"
#include "ecs/system.hpp"
#include "ecs/world.hpp"
#include "refl/type.hpp"

#include <cstddef>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace fei {

// Default: T=component
template<typename T>
struct QueryData {
    using Component = std::remove_cv_t<std::remove_reference_t<T>>;

    static bool match(const Archetype& archetype) {
        return archetype.has_component(type_id<Component>());
    }
    static decltype(auto)
    get(Archetype& archetype, std::size_t index, SystemTicks system_ticks) {
        auto ref = archetype.get_component(type_id<Component>(), index);
        if constexpr (std::is_const_v<std::remove_reference_t<T>>) {
            return ref.template get_const<Component>();
        } else {
            return ComponentRW<Component>(
                ref.template get<Component>(),
                archetype.component_ticks(type_id<Component>(), index),
                system_ticks
            );
        }
    }
};

template<>
struct QueryData<Entity> {
    static bool match(const Archetype& archetype) { return true; }
    static Entity
    get(const Archetype& archetype, std::size_t index, SystemTicks) {
        return archetype.entities()[index];
    }
};

template<typename T>
struct With {};

template<typename T>
struct Without {};

template<typename T>
struct Added {};

template<typename T>
struct Changed {};

template<typename... Filters>
struct Or {};

template<typename T>
struct QueryFilter;

template<typename T>
struct QueryFilter<With<T>> {
    static bool match(const Archetype& archetype) {
        return archetype.has_component(type_id<T>());
    }
    static bool match_row(const Archetype&, std::size_t, SystemTicks) {
        return true;
    }
};

template<typename T>
struct QueryFilter<Without<T>> {
    static bool match(const Archetype& archetype) {
        return !archetype.has_component(type_id<T>());
    }
    static bool match_row(const Archetype&, std::size_t, SystemTicks) {
        return true;
    }
};

template<typename T>
struct QueryFilter<Added<T>> {
    static bool match(const Archetype& archetype) {
        return archetype.has_component(type_id<T>());
    }
    static bool match_row(
        const Archetype& archetype,
        std::size_t row,
        SystemTicks system_ticks
    ) {
        return archetype.component_ticks(type_id<T>(), row)
            .is_added(system_ticks);
    }
};

template<typename T>
struct QueryFilter<Changed<T>> {
    static bool match(const Archetype& archetype) {
        return archetype.has_component(type_id<T>());
    }
    static bool match_row(
        const Archetype& archetype,
        std::size_t row,
        SystemTicks system_ticks
    ) {
        return archetype.component_ticks(type_id<T>(), row)
            .is_changed(system_ticks);
    }
};

template<typename... Filters>
struct QueryFilter<Or<Filters...>> {
    static bool match(const Archetype& archetype) {
        return (QueryFilter<Filters>::match(archetype) || ...);
    }
    static bool match_row(
        const Archetype& archetype,
        std::size_t row,
        SystemTicks system_ticks
    ) {
        return (
            (QueryFilter<Filters>::match(archetype) &&
             QueryFilter<Filters>::match_row(archetype, row, system_ticks)) ||
            ...
        );
    }
};

class QueryBase {
  protected:
    World* m_world {nullptr};
    std::vector<ArchetypeId> m_cached_archetypes;
    SystemTicks m_system_ticks;

    virtual bool match_row(const Archetype&, std::size_t) const { return true; }

  public:
    virtual ~QueryBase() = default;

    World& world() const { return *m_world; }
    const std::vector<ArchetypeId>& matching_archetypes() const {
        return m_cached_archetypes;
    }
    SystemTicks system_ticks() const { return m_system_ticks; }
    bool matches_row(const Archetype& archetype, std::size_t row) const {
        return match_row(archetype, row);
    }
};

template<typename... Datas>
class QueryIter {
  private:
    const QueryBase* m_query;
    std::size_t m_archetype_index {0};
    std::size_t m_entity_index {0};

  public:
    using value_type = std::tuple<decltype(QueryData<Datas>::get(
        std::declval<Archetype&>(),
        0,
        std::declval<SystemTicks>()
    ))...>;

    QueryIter(const QueryBase* query, bool is_end = false) : m_query(query) {
        if (!is_end) {
            advance_to_next_valid();
        } else {
            m_archetype_index = m_query->matching_archetypes().size();
        }
    }

    // Dereference operator returns tuple of component references
    value_type operator*() const {
        auto& archetype = m_query->world().archetypes().get(
            m_query->matching_archetypes()[m_archetype_index]
        );
        return std::tuple_cat(
            get_component_tuple<Datas>(archetype, m_entity_index)...
        );
    }

    // Pre-increment operator
    QueryIter& operator++() {
        m_entity_index++;
        advance_to_next_valid();
        return *this;
    }

    // Equality comparison
    bool operator==(const QueryIter& other) const {
        return m_archetype_index == other.m_archetype_index &&
               m_entity_index == other.m_entity_index;
    }

    // Inequality comparison
    bool operator!=(const QueryIter& other) const { return !(*this == other); }

  private:
    template<typename T>
    auto get_component_tuple(Archetype& archetype, std::size_t index) const {
        using Result = decltype(QueryData<T>::get(
            archetype,
            index,
            m_query->system_ticks()
        ));
        return std::tuple<Result>(
            QueryData<T>::get(archetype, index, m_query->system_ticks())
        );
    }
    void advance_to_next_valid() {
        while (m_archetype_index < m_query->matching_archetypes().size()) {
            const auto& archetype = m_query->world().archetypes().get(
                m_query->matching_archetypes()[m_archetype_index]
            );
            while (m_entity_index < archetype.size()) {
                if (m_query->matches_row(archetype, m_entity_index)) {
                    return;
                }
                ++m_entity_index;
            }
            // Move to next archetype
            m_archetype_index++;
            m_entity_index = 0;
        }
    }
};

template<typename... Datas>
class Query;

template<typename Q, typename... Filters>
    requires SpecializationOf<Q, Query>
class FilteredQuery;

template<typename... Datas>
class Query : public QueryBase {
  public:
    using Iterator = QueryIter<Datas...>;

    template<typename... Filters>
    using Filter = FilteredQuery<Query<Datas...>, Filters...>;

    // Prepare the query with world context
    static Query get_param(World& world, SystemTicks system_ticks) {
        Query query;
        query.m_world = &world;
        query.m_system_ticks = system_ticks;
        query.update_cache();
        return query;
    }

    // Get iterator to beginning of query results
    Iterator begin() const { return Iterator(this, false); }

    // Get iterator to end of query results
    Iterator end() const { return Iterator(this, true); }

    // Check if query has any results
    bool empty() const { return begin() == end(); }

    // Get count of matching entities
    std::size_t size() const {
        std::size_t count = 0;
        for (auto it = begin(); it != end(); ++it) {
            ++count;
        }
        return count;
    }

    Iterator::value_type first() const {
        if (empty()) {
            throw std::runtime_error("Query is empty");
        }
        return *begin();
    }

  protected:
    virtual bool match_archetype(const Archetype& archetype) const {
        return (QueryData<Datas>::match(archetype) && ...);
    }

    void update_cache() {
        m_cached_archetypes.clear();

        for (const auto& [archetype_id, archetype] : m_world->archetypes()) {
            if (match_archetype(archetype)) {
                m_cached_archetypes.push_back(archetype_id);
            }
        }
    }
};
template<typename... Datas>
struct SystemParamTraits<Query<Datas...>> {
    using State = std::monostate;

    static State init_state(World&) { return {}; }

    static Query<Datas...>
    get_param(World& world, State&, SystemTicks system_ticks) {
        return Query<Datas...>::get_param(world, system_ticks);
    }
};

template<typename Q, typename... Filters>
    requires SpecializationOf<Q, Query>
class FilteredQuery : public Q {
  public:
    using Iterator = Q::Iterator;

    static FilteredQuery get_param(World& world, SystemTicks system_ticks) {
        FilteredQuery query;
        query.m_world = &world;
        query.m_system_ticks = system_ticks;
        query.update_cache();
        return query;
    }

  protected:
    bool match_archetype(const Archetype& archetype) const override {
        return Q::match_archetype(archetype) &&
               (QueryFilter<Filters>::match(archetype) && ...);
    }

    bool match_row(const Archetype& archetype, std::size_t row) const override {
        return (
            QueryFilter<Filters>::match_row(
                archetype,
                row,
                this->m_system_ticks
            ) &&
            ...
        );
    }
};
template<typename Q, typename... Filters>
    requires SpecializationOf<Q, Query>
struct SystemParamTraits<FilteredQuery<Q, Filters...>> {
    using State = std::monostate;

    static State init_state(World&) { return {}; }

    static FilteredQuery<Q, Filters...>
    get_param(World& world, State&, SystemTicks system_ticks) {
        return FilteredQuery<Q, Filters...>::get_param(world, system_ticks);
    }
};

} // namespace fei
