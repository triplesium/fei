#pragma once

#include "base/debug.hpp"
#include "ecs/column.hpp"
#include "ecs/fwd.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace fei {

inline std::size_t hash_type_ids(const std::vector<TypeId>& type_ids) {
    std::size_t seed = 0;
    for (auto& type : type_ids) {
        auto x = type.id();
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = ((x >> 16) ^ x) * 0x45d9f3b;
        x = (x >> 16) ^ x;
        seed ^= x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    return seed;
}

struct Edges {
    std::unordered_map<TypeId, ArchetypeId> remove;
    std::unordered_map<TypeId, ArchetypeId> add;
};

class Archetype {
  private:
    ArchetypeId m_id;
    std::vector<TypeId> m_components;
    std::vector<Entity> m_entities;
    std::unordered_map<TypeId, Column> m_columns;
    Edges m_edges;

  public:
    Archetype(ArchetypeId id, std::vector<TypeId> components) :
        m_id(id), m_components(components) {
        std::sort(m_components.begin(), m_components.end());
        for (auto& type : components) {
            m_columns.emplace(type, Column(type));
        }
    }

    ArchetypeId id() const { return m_id; }
    std::size_t size() const { return m_entities.size(); }

    std::size_t alloc(Entity entity) {
        m_entities.push_back(entity);
        for (auto& type : m_components) {
            auto& column = m_columns.at(type);
            column.push_back(nullptr);
        }
        return m_entities.size() - 1;
    }

    void remove_row(std::size_t row) {
        for (auto& [type, column] : m_columns) {
            column.swap_remove(static_cast<uint32_t>(row));
        }
        m_entities[row] = m_entities.back();
        m_entities.pop_back();
    }

    void remove_entity(Entity entity) {
        auto it = std::find(m_entities.begin(), m_entities.end(), entity);
        if (it == m_entities.end()) {
            error("Entity {} not found in archetype {}", entity, m_id);
            return;
        }
        std::size_t row = std::distance(m_entities.begin(), it);
        remove_row(row);
    }

    Ref get_component(TypeId type_id, std::size_t row) const {
        if (!m_columns.contains(type_id)) {
            return nullptr;
        }
        return m_columns.at(type_id).get(static_cast<uint32_t>(row));
    }

    void set_component(TypeId type_id, std::size_t row, Ref ref) {
        if (!m_columns.contains(type_id)) {
            return;
        }
        m_columns.at(type_id).set(static_cast<uint32_t>(row), ref);
    }

    bool has_component(TypeId type_id) const {
        return m_columns.contains(type_id);
    }

    std::size_t hash() const { return hash_type_ids(m_components); }

    const std::vector<Entity>& entities() const { return m_entities; }
    const std::vector<TypeId>& components() const { return m_components; }
    Column& column(TypeId type_id) { return m_columns.at(type_id); }
    Edges& edges() { return m_edges; }
};

class Archetypes {
  private:
    std::unordered_map<ArchetypeId, Archetype> m_archetypes;
    std::unordered_map<std::size_t, ArchetypeId> m_hashes;

  public:
    ~Archetypes() = default;
    ArchetypeId get_id_or_insert(std::vector<TypeId> components) {
        auto hash = hash_type_ids(components);
        if (m_hashes.contains(hash)) {
            return m_hashes[hash];
        }
        ArchetypeId id = static_cast<ArchetypeId>(m_archetypes.size() + 1);
        m_archetypes.emplace(id, Archetype(id, components));
        m_hashes[hash] = id;
        return id;
    }

    Archetype& get(ArchetypeId id) {
        FEI_ASSERT(m_archetypes.contains(id));
        return m_archetypes.at(id);
    }

    const Archetype& get(ArchetypeId id) const {
        FEI_ASSERT(m_archetypes.contains(id));
        return m_archetypes.at(id);
    }

    // Add iterator access for query system
    auto begin() const { return m_archetypes.begin(); }
    auto end() const { return m_archetypes.end(); }
};

} // namespace fei
