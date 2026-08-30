#include "public_type_names.hpp"

#include "Luau/GlobalTypes.h"
#include "Luau/Type.h"

#include <string_view>

namespace ets::lsp {

void apply_public_type_names(Luau::GlobalTypes& globals) {
    constexpr std::string_view internal_prefix = "__Entisium_";
    for (const auto& [public_name, type_function] :
         globals.globalScope->exportedTypeBindings) {
        auto* external = Luau::getMutable<Luau::ExternType>(
            Luau::follow(type_function.type)
        );
        if (external != nullptr &&
            std::string_view {external->name}.starts_with(internal_prefix)) {
            external->name = public_name;
        }
    }
}

} // namespace ets::lsp
