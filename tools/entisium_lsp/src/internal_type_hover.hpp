#pragma once

#include "Luau/Location.h"

#include <optional>
#include <string>
#include <vector>

namespace Luau {
struct GlobalTypes;
struct Module;
struct SourceModule;
} // namespace Luau

namespace ets::lsp {

struct InternalTypeAlias {
    std::string marker_property;
    std::string alias_name;
};

using InternalTypeAliases = std::vector<InternalTypeAlias>;

[[nodiscard]] InternalTypeAliases
collect_internal_type_aliases(const Luau::GlobalTypes& globals);

void update_internal_type_aliases(
    InternalTypeAliases& aliases,
    const Luau::GlobalTypes& globals
);

[[nodiscard]] std::optional<std::string> internal_hover_signature(
    const Luau::SourceModule& source_module,
    const Luau::Module& module,
    Luau::Position position,
    const InternalTypeAliases& aliases,
    bool hide_table_kind
);

[[nodiscard]] std::optional<std::string> internal_function_return_type(
    const Luau::SourceModule& source_module,
    const Luau::Module& module,
    Luau::Position position,
    const InternalTypeAliases& aliases,
    bool hide_table_kind
);

} // namespace ets::lsp
