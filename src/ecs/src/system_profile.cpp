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

#include <array>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace fei {
namespace {

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

#if defined(_WIN32)

constexpr std::size_t max_symbol_name = 1024;

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

} // namespace fei
