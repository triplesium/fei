#pragma once

#include "base/result.hpp"
#include "base/types.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <nlohmann/json.hpp> // IWYU pragma: keep
#include <optional>
#include <string>
#include <string_view>

namespace fei::agentd {

struct PlayControlBindings {
    std::function<Result<nlohmann::json, std::string>()> interfaces;
    std::function<Result<nlohmann::json, std::string>(
        std::string_view,
        const nlohmann::json&,
        std::optional<uint32>
    )>
        step;
    std::function<Result<nlohmann::json, std::string>(std::string_view)>
        observe;
    std::function<
        Result<nlohmann::json, std::string>(const std::optional<std::string>&)>
        capture;
};

struct PlayRunLimits {
    std::size_t maximum_source_bytes {std::size_t {64} * 1024};
    std::size_t maximum_memory_bytes {std::size_t {16} * 1024 * 1024};
    std::size_t maximum_calls {128};
    uint64 maximum_ticks {10'000};
    std::size_t maximum_interrupts {1'000'000};
    std::chrono::milliseconds maximum_duration {std::chrono::seconds(30)};
};

struct PlayRunObserver {
    std::function<void(std::size_t, std::string_view, const nlohmann::json&)>
        call_started;
    std::function<void(std::size_t)> call_finished;
    std::function<void(const nlohmann::json&)> log;
};

[[nodiscard]] nlohmann::json run_luau_play_script(
    std::string_view source,
    const PlayControlBindings& bindings,
    const PlayRunLimits& limits = {},
    const PlayRunObserver& observer = {}
);

} // namespace fei::agentd
