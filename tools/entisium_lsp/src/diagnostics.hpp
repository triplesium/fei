#pragma once

#include "Protocol/Diagnostics.hpp"

#include <vector>

class TextDocument;

namespace Luau {
struct SourceModule;
}

namespace ets::lsp {

class ScriptTypeRegistry;

void add_entisium_diagnostics(
    const TextDocument& document,
    const Luau::SourceModule& source_module,
    std::vector<::lsp::Diagnostic>& diagnostics,
    const ScriptTypeRegistry* script_types = nullptr
);

} // namespace ets::lsp
