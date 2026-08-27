#include "ecs/system_profile.hpp"

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    define NOMINMAX
// DbgHelp.h depends on Windows.h definitions.
// clang-format off
// NOLINTNEXTLINE(misc-include-cleaner)
#    include <Windows.h>
#    include <DbgHelp.h>
// clang-format on
#endif

#if defined(__EMSCRIPTEN__)
extern "C" std::uint32_t
ets_profile_wasm_function_index(std::uint32_t table_index);
extern "C" std::uint32_t
ets_profile_wasm_build_id(char* output, std::uint32_t capacity);
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#if defined(__EMSCRIPTEN__)
#    include <limits>
#endif

namespace ets {
namespace {

#if defined(_WIN32)

std::string strip_template_arguments(std::string_view symbol) {
    std::string result;
    result.reserve(symbol.size());

    int depth = 0;
    for (auto ch : symbol) {
        if (ch == '<') {
            ++depth;
            continue;
        }
        if (ch == '>' && depth > 0) {
            --depth;
            continue;
        }
        if (depth == 0) {
            result.push_back(ch);
        }
    }
    return result;
}

std::string leaf_symbol_name(std::string_view symbol) {
    auto paren = symbol.find('(');
    if (paren != std::string_view::npos) {
        symbol = symbol.substr(0, paren);
    }

    auto stripped = strip_template_arguments(symbol);
    symbol = stripped;

    auto scope = symbol.rfind("::");
    if (scope != std::string_view::npos) {
        symbol = symbol.substr(scope + 2);
    } else {
        auto space = symbol.find_last_of(' ');
        if (space != std::string_view::npos) {
            symbol = symbol.substr(space + 1);
        }
    }
    return std::string(symbol);
}

constexpr std::size_t max_symbol_name = 1024;

struct CodeViewPdb70 {
    DWORD signature;
    GUID guid;
    DWORD age;
};

constexpr DWORD code_view_pdb70_signature = 0x53445352;

std::string pdb_module_id(const CodeViewPdb70& code_view) {
    const auto& guid = code_view.guid;
    std::array<char, 96> buffer {};
    std::snprintf(
        buffer.data(),
        buffer.size(),
        "pdb:%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x:%lx",
        static_cast<unsigned long>(guid.Data1),
        static_cast<unsigned>(guid.Data2),
        static_cast<unsigned>(guid.Data3),
        static_cast<unsigned>(guid.Data4[0]),
        static_cast<unsigned>(guid.Data4[1]),
        static_cast<unsigned>(guid.Data4[2]),
        static_cast<unsigned>(guid.Data4[3]),
        static_cast<unsigned>(guid.Data4[4]),
        static_cast<unsigned>(guid.Data4[5]),
        static_cast<unsigned>(guid.Data4[6]),
        static_cast<unsigned>(guid.Data4[7]),
        static_cast<unsigned long>(code_view.age)
    );
    return buffer.data();
}

std::string pe_module_id(HMODULE module) {
    const auto* base = reinterpret_cast<const std::byte*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return {};
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        base + static_cast<std::size_t>(dos->e_lfanew)
    );
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return {};
    }

    const auto& debug_data =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (debug_data.VirtualAddress != 0 &&
        debug_data.Size >= sizeof(IMAGE_DEBUG_DIRECTORY)) {
        const auto* debug_entries =
            reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(
                base + debug_data.VirtualAddress
            );
        const auto entry_count =
            debug_data.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
        for (DWORD index = 0; index < entry_count; ++index) {
            const auto& entry = debug_entries[index];
            if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW ||
                entry.AddressOfRawData == 0 ||
                entry.SizeOfData < sizeof(CodeViewPdb70)) {
                continue;
            }
            const auto* code_view = reinterpret_cast<const CodeViewPdb70*>(
                base + entry.AddressOfRawData
            );
            if (code_view->signature == code_view_pdb70_signature) {
                return pdb_module_id(*code_view);
            }
        }
    }

    std::array<char, 64> fallback {};
    std::snprintf(
        fallback.data(),
        fallback.size(),
        "pe:%08lx:%08lx",
        static_cast<unsigned long>(nt->FileHeader.TimeDateStamp),
        static_cast<unsigned long>(nt->OptionalHeader.SizeOfImage)
    );
    return fallback.data();
}

ProfileSymbolRef profile_symbol_windows(std::size_t address) {
    HMODULE module = nullptr;
    const auto* address_pointer = reinterpret_cast<const char*>(address);
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            address_pointer,
            &module
        ) != TRUE ||
        module == nullptr) {
        return {};
    }

    const auto module_base = reinterpret_cast<std::uintptr_t>(module);
    if (address < module_base) {
        return {};
    }
    return ProfileSymbolRef {
        .kind = ProfileSymbolKind::PeRva,
        .module_id = pe_module_id(module),
        .value = address - module_base,
    };
}

std::string undecorate_symbol(std::string_view symbol) {
    std::string input(symbol);
    std::array<char, max_symbol_name> undecorated {};
    if (UnDecorateSymbolName(
            input.c_str(),
            undecorated.data(),
            static_cast<DWORD>(undecorated.size()),
            UNDNAME_COMPLETE
        ) != 0) {
        return undecorated.data();
    }
    return input;
}

std::optional<std::string>
extract_incremental_link_symbol(std::string_view symbol) {
    if (symbol.starts_with("@ILT+")) {
        symbol.remove_prefix(1);
    }

    if (!symbol.starts_with("ILT+")) {
        return std::nullopt;
    }

    const auto open = symbol.find('(');
    if (open == std::string_view::npos || open + 1 >= symbol.size()) {
        return std::nullopt;
    }

    auto close = symbol.rfind(')');
    if (close == std::string_view::npos || close <= open) {
        close = symbol.size();
    }

    auto decorated = symbol.substr(open + 1, close - open - 1);
    if (decorated.empty()) {
        return std::nullopt;
    }
    return std::string(decorated);
}

std::optional<DWORD64>
address_for_symbol(HANDLE process, std::string_view symbol_name) {
    std::string input(symbol_name);
    alignas(SYMBOL_INFO)
        std::array<std::byte, sizeof(SYMBOL_INFO) + max_symbol_name>
            storage {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage.data());
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = max_symbol_name;

    if (SymFromName(process, input.c_str(), symbol) != TRUE) {
        return std::nullopt;
    }
    return symbol->Address;
}

bool resolve_line_info(
    HANDLE process,
    DWORD64 address,
    SystemProfileInfo& info
) {
    IMAGEHLP_LINE64 line {};
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    DWORD line_displacement = 0;
    if (SymGetLineFromAddr64(process, address, &line_displacement, &line) !=
        TRUE) {
        return false;
    }

    info.file = line.FileName ? line.FileName : "<unknown>";
    info.line = line.LineNumber;
    return true;
}

std::optional<SystemProfileInfo> symbolize_windows(std::size_t address) {
    static std::mutex dbghelp_mutex;
    static bool initialized = false;

    std::scoped_lock lock(dbghelp_mutex);

    HANDLE process = GetCurrentProcess();
    if (!initialized) {
        auto options =
            SymGetOptions() | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES;
        options &= ~SYMOPT_UNDNAME;
        SymSetOptions(options);
        initialized = SymInitialize(process, nullptr, TRUE) == TRUE;
    }
    if (!initialized) {
        return std::nullopt;
    }

    alignas(SYMBOL_INFO)
        std::array<std::byte, sizeof(SYMBOL_INFO) + max_symbol_name>
            storage {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage.data());
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = max_symbol_name;

    DWORD64 displacement = 0;
    const auto original_address = static_cast<DWORD64>(address);
    if (SymFromAddr(process, original_address, &displacement, symbol) != TRUE) {
        return std::nullopt;
    }

    std::string function_symbol = symbol->Name;
    auto line_address = original_address;
    if (auto ilt_symbol = extract_incremental_link_symbol(function_symbol)) {
        function_symbol = std::move(*ilt_symbol);
        if (auto target_address =
                address_for_symbol(process, function_symbol)) {
            line_address = *target_address;
        }
    }

    auto function = undecorate_symbol(function_symbol);
    SystemProfileInfo info {
        .name = leaf_symbol_name(function),
        .file = "<unknown>",
        .function = std::move(function),
        .line = 0,
    };

    if (!resolve_line_info(process, line_address, info) &&
        line_address != original_address) {
        resolve_line_info(process, original_address, info);
    }

    return info;
}

#endif

#if defined(__EMSCRIPTEN__)

std::string wasm_module_id() {
    std::array<char, 96> buffer {};
    const auto length = ets_profile_wasm_build_id(
        buffer.data(),
        static_cast<std::uint32_t>(buffer.size())
    );
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    return std::string(buffer.data(), length);
}

ProfileSymbolRef profile_symbol_wasm(std::size_t table_index) {
    static const std::string module_id = wasm_module_id();
    const auto function_index = ets_profile_wasm_function_index(
        static_cast<std::uint32_t>(table_index)
    );
    if (module_id.empty() ||
        function_index == std::numeric_limits<std::uint32_t>::max()) {
        return {};
    }
    return ProfileSymbolRef {
        .kind = ProfileSymbolKind::WasmFunctionIndex,
        .module_id = module_id,
        .value = function_index,
    };
}

#endif

} // namespace

SystemProfileRegistry& SystemProfileRegistry::instance() {
    static SystemProfileRegistry registry;
    return registry;
}

void SystemProfileRegistry::register_system(
    std::size_t key,
    SystemProfileInfo info
) {
    m_profiles[key] = std::move(info);
}

auto SystemProfileRegistry::find(std::size_t key) const
    -> std::optional<SystemProfileInfo> {
    auto it = m_profiles.find(key);
    if (it == m_profiles.end()) {
        return std::nullopt;
    }
    return it->second;
}

ProfileSymbolRef SystemProfileRegistry::symbol_ref(std::size_t address) const {
#if defined(__EMSCRIPTEN__)
    return profile_symbol_wasm(address);
#elif defined(_WIN32)
    return profile_symbol_windows(address);
#else
    (void)address;
    return {};
#endif
}

std::optional<SystemProfileInfo>
SystemProfileRegistry::symbolize(std::size_t address) const {
#if defined(_WIN32)
    return symbolize_windows(address);
#else
    (void)address;
    return std::nullopt;
#endif
}

void SystemProfileRegistry::clear() {
    m_profiles.clear();
}

} // namespace ets
