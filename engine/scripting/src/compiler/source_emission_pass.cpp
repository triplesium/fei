#include "pass.hpp"

#include <utility>

namespace ets::detail::luau_compiler {
Result<std::string, LuauScriptError> RuntimeSourceEmissionPass::run(
    const LuauScriptSource& source,
    std::span<const std::string> functions,
    SourcePatchSet patches
) const {
    auto generated = patches.apply(source);
    if (!generated) {
        return failure(std::move(generated.error()));
    }
    generated->append("\nexport const __ets_functions = {\n");
    for (const auto& function : functions) {
        generated->append("    ");
        generated->append(function);
        generated->append(",\n");
    }
    generated->append("}\n");
    return std::move(*generated);
}

} // namespace ets::detail::luau_compiler
