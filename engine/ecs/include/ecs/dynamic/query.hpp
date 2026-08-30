#pragma once

#include "ecs/change_detection.hpp"
#include "ecs/dynamic/access.hpp"
#include "ecs/dynamic/system_param.hpp"
#include "ecs/fwd.hpp"
#include "refl/ref.hpp"
#include "refl/type.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace ets {

class Archetype;
class Column;
class World;

enum class DynamicQueryFieldKind {
    Component,
    Entity,
};

struct DynamicQueryField {
    std::string name;
    TypeId type;
    DynamicParamAccess access {DynamicParamAccess::Read};
    DynamicQueryFieldKind kind {DynamicQueryFieldKind::Component};
};

struct DynamicQueryFilter {
    enum class Kind {
        With,
        Without,
        Added,
        Changed,
        Or,
    };

    Kind kind {Kind::With};
    TypeId type;
    std::vector<DynamicQueryFilter> filters;
    bool required {true};
};

struct DynamicQueryCursor {
    std::size_t archetype_index {0};
    std::size_t row {0};
};

struct DynamicQueryRow {
    ArchetypeId archetype {};
    std::size_t prepared_archetype_index {0};
    std::size_t row {0};
};

struct DynamicQueryFieldBorrow {
    Ref value;
    ComponentTicks* ticks {nullptr};
    Tick change_tick {0};
};

class DynamicQuery final : public DynamicSystemParam {
  public:
    std::string name;

  private:
    struct PreparedField {
        const Column* read_column {nullptr};
        Column* write_column {nullptr};
    };

    struct PreparedArchetype {
        ArchetypeId id {};
        Archetype* archetype {nullptr};
        std::vector<PreparedField> fields;
    };

    World* m_world {nullptr};
    std::vector<DynamicQueryField> m_fields;
    std::vector<DynamicQueryFilter> m_filters;
    std::vector<PreparedArchetype> m_prepared_archetypes;
    SystemTicks m_system_ticks;

  public:
    using DynamicSystemParam::prepare;

    DynamicQuery(
        std::string name,
        std::vector<DynamicQueryField> fields,
        std::vector<DynamicQueryFilter> filters
    );

    SystemAccess access() const override;
    Result<Ref, DynamicSystemError>
    prepare(World& world, SystemTicks system_ticks) override;

    bool next(DynamicQueryCursor& cursor, DynamicQueryRow& row) const;
    Ref field(const DynamicQueryRow& row, std::size_t field_index) const;
    DynamicQueryFieldBorrow
    field_untracked(const DynamicQueryRow& row, std::size_t field_index) const;

    const std::vector<DynamicQueryField>& fields() const { return m_fields; }
    std::size_t size() const;
    std::uint64_t runtime_state_type() const override;

  private:
    void refresh(World& world);
    bool matches(const Archetype& archetype) const;
    bool matches_archetype(
        const DynamicQueryFilter& filter,
        const Archetype& archetype
    ) const;
    bool matches_row(
        const DynamicQueryFilter& filter,
        const Archetype& archetype,
        std::size_t row
    ) const;
};

} // namespace ets
