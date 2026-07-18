#pragma once

#include "ecs/dynamic/access.hpp"
#include "ecs/dynamic/system_param.hpp"
#include "ecs/fwd.hpp"
#include "refl/ref.hpp"
#include "refl/type.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace fei {

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
    TypeId type;
    bool required {true};
};

struct DynamicQueryCursor {
    std::size_t archetype_index {0};
    std::size_t row {0};
};

struct DynamicQueryRow {
    ArchetypeId archetype {};
    std::size_t row {0};
};

class DynamicQuery final : public DynamicSystemParam {
  public:
    std::string name;

  private:
    World* m_world {nullptr};
    std::vector<DynamicQueryField> m_fields;
    std::vector<DynamicQueryFilter> m_filters;
    std::vector<ArchetypeId> m_matching_archetypes;
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

    const std::vector<DynamicQueryField>& fields() const { return m_fields; }
    std::size_t size() const;

  private:
    void refresh(World& world);
    bool matches(ArchetypeId archetype_id) const;
};

} // namespace fei
