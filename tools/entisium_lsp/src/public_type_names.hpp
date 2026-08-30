#pragma once

namespace Luau {
struct GlobalTypes;
} // namespace Luau

namespace ets::lsp {

void apply_public_type_names(Luau::GlobalTypes& globals);

} // namespace ets::lsp
