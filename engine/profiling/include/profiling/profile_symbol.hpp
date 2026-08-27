#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ets {

enum class ProfileSymbolKind : std::uint8_t {
    None,
    PeRva,
    WasmFunctionIndex,
};

[[nodiscard]] constexpr std::string_view
profile_symbol_kind_name(ProfileSymbolKind kind) {
    switch (kind) {
        case ProfileSymbolKind::PeRva:
            return "pe-rva";
        case ProfileSymbolKind::WasmFunctionIndex:
            return "wasm-function-index";
        case ProfileSymbolKind::None:
            return "none";
    }
    return "none";
}

struct ProfileSymbolRef {
    ProfileSymbolKind kind {ProfileSymbolKind::None};
    std::string module_id;
    std::uint64_t value {0};

    [[nodiscard]] bool valid() const {
        return kind != ProfileSymbolKind::None && !module_id.empty();
    }
};

} // namespace ets
