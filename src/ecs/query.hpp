#pragma once
#include "base/concepts.hpp"
#include "ecs/archetype.hpp"
#include "ecs/fwd.hpp"
#include "ecs/system.hpp"
#include "ecs/world.hpp"
#include "refl/type.hpp"

#include <cstddef>
#include <tuple>
#include <vector>

namespace fei {

// Default: T=component
template<typename T>
struct QueryData {
    static bool match(const Archetype& archetype) {
        return archetype.has_component(type_id<T>());
    }
    static T& get(const Archetype& archetype, std::size_t index) {
        return archetype.get_component(type_id<T>(), index).template get<T>();
    }
};

template<>
struct QueryData<Entity> {
    static bool match(const Archetype& archetype) { return true; }
    static Entity get(const Archetype& archetype, std::size_t index) {
        return archetype.entities()[index];
    }
};

template<typename T>
struct With {};

template<typename T>
struct Without {};

template<typename T>
struct QueryFilter;

template<typename T>
struct QueryFilter<With<T>> {
    static bool match(const Archetype& archetype) {
        return archetype.has_component(type_id<T>());
    }
};

template<typename T>
struct QueryFilter<Without<T>> {
    static bool match(const Archetype& archetype) {
        return !archetype.has_component(type_id<T>());
    }
};

template<typename... Datas>
class QueryIter {
  private:
    const World* m_world;
    const std::vector<ArchetypeId>* m_matching_archetypes;
    std::size_t m_archetype_index;
    std::size_t m_entity_index;

  public:
    using value_type =
        std::tuple<decltype(QueryData<Datas>::get(std::declval<Archetype>(), 0)
        )...>;

    QueryIter(
        const World* world,
        const std::vector<ArchetypeId>* matching_archetypes,
        bool is_end = false
    ) :
        m_world(world), m_matching_archetypes(matching_archetypes),
        m_archetype_index(0), m_entity_index(0) {
        if (!is_end) {
            advance_to_next_valid();
        } else {
            m_archetype_index = m_matching_archetypes->size();
        }
    }

    // Dereference operator returns tuple of component references
    value_type operator*() const {
        const auto& archetype =
            m_world->archetypes().get((*m_matching_archetypes
            )[m_archetype_index]);
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
    auto
    get_component_tuple(const Archetype& archetype, std::size_t index) const {
        return std::tuple<decltype(QueryData<T>::get(archetype, index))>(
            QueryData<T>::get(archetype, index)
        );
    }
    void advance_to_next_valid() {
        while (m_archetype_index < m_matching_archetypes->size()) {
            const auto& archetype =
                m_world->archetypes().get((*m_matching_archetypes
                )[m_archetype_index]);
            if (m_entity_index < archetype.size()) {
                return; // Found valid entity
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
class Query {
  protected:
    const World* m_world = nullptr;
    std::vector<ArchetypeId> m_cached_archetypes;

  public:
    using Iterator = QueryIter<Datas...>;

    template<typename... Filters>
    using Filter = FilteredQuery<Query<Datas...>, Filters...>;

    // Prepare the query with world context
    static Query get_param(World& world) {
        Query query;
        query.m_world = &world;
        query.update_cache();
        return query;
    }

    // Get iterator to beginning of query results
    Iterator begin() const {
        return Iterator(m_world, &m_cached_archetypes, false);
    }

    // Get iterator to end of query results
    Iterator end() const {
        return Iterator(m_world, &m_cached_archetypes, true);
    }

    // Check if query has any results
    bool empty() const { return m_cached_archetypes.empty(); }

    // Get count of matching entities
    std::size_t size() const {
        std::size_t count = 0;
        for (ArchetypeId archetype_id : m_cached_archetypes) {
            const auto& archetype = m_world->archetypes().get(archetype_id);
            count += archetype.size();
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

template<typename Q, typename... Filters>
    requires SpecializationOf<Q, Query>
class FilteredQuery : public Q {
  public:
    using Iterator = Q::Iterator;

    static FilteredQuery get_param(World& world) {
        FilteredQuery query;
        query.m_world = &world;
        query.update_cache();
        return query;
    }

  protected:
    virtual bool match_archetype(const Archetype& archetype) const override {
        return Q::match_archetype(archetype) &&
               (QueryFilter<Filters>::match(archetype) && ...);
    }
};

} // namespace fei
