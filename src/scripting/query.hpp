#pragma once

#include "ecs/fwd.hpp"
#include "refl/ref.hpp"
#include "refl/type.hpp"
#include "scripting/runtime.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace fei {

class World;

struct ScriptQueryField {
    std::string name;
    TypeId type;
    ScriptSystemAccess access {ScriptSystemAccess::Read};
};

struct ScriptQueryFilter {
    TypeId type;
    bool required {true};
};

struct ScriptQueryCursor {
    std::size_t archetype_index {0};
    std::size_t row {0};
};

struct ScriptQueryRow {
    ArchetypeId archetype {};
    std::size_t row {0};
};

class ScriptQuery {
  private:
    World* m_world {nullptr};
    std::vector<ScriptQueryField> m_fields;
    std::vector<ScriptQueryFilter> m_filters;
    std::vector<ArchetypeId> m_matching_archetypes;

  public:
    ScriptQuery(
        World& world,
        std::vector<ScriptQueryField> fields,
        std::vector<ScriptQueryFilter> filters
    );

    bool next(ScriptQueryCursor& cursor, ScriptQueryRow& row) const;
    Ref field(const ScriptQueryRow& row, std::size_t field_index) const;

    const std::vector<ScriptQueryField>& fields() const { return m_fields; }
    std::size_t size() const;

  private:
    bool matches(ArchetypeId archetype_id) const;
};

} // namespace fei
