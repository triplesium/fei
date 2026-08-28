#include "profiling/profiling.hpp"

#include <algorithm>
#include <array>
#include <barrier>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct Options {
    std::size_t scopes_per_thread {250'000};
    std::size_t threads {
        std::max<std::size_t>(1, std::thread::hardware_concurrency())
    };
};

constexpr std::size_t systems_per_thread = 64;
using ThreadRecordIds = std::array<ets::ProfileRecordId, systems_per_thread>;

std::size_t parse_size(std::string_view value, std::string_view option) {
    std::size_t parsed = 0;
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc {} || end != value.data() + value.size() ||
        parsed == 0) {
        std::cerr << "invalid value for " << option << ": " << value << '\n';
        std::exit(2);
    }
    return parsed;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--scopes" && index + 1 < argc) {
            options.scopes_per_thread = parse_size(argv[++index], argument);
        } else if (argument == "--threads" && index + 1 < argc) {
            options.threads = parse_size(argv[++index], argument);
        } else {
            std::cerr << "unknown benchmark option: " << argument << '\n';
            std::exit(2);
        }
    }
    return options;
}

std::vector<ThreadRecordIds> register_records(std::size_t threads) {
    std::vector<ThreadRecordIds> records(threads);
    for (std::size_t thread = 0; thread < threads; ++thread) {
        for (std::size_t system = 0; system < systems_per_thread; ++system) {
            records[thread][system] = ets::register_system_profile_record(
                1,
                thread * systems_per_thread + system,
                nullptr,
                "benchmark_system",
                "profiling_benchmark.cpp",
                "benchmark_system",
                1
            );
        }
    }
    return records;
}

void run_scopes(
    std::size_t thread_index,
    const ThreadRecordIds& record_ids,
    std::size_t count
) {
    for (std::size_t index = 0; index < count; ++index) {
        const auto local_system = index % systems_per_thread;
        const auto system_id = thread_index * systems_per_thread + local_system;
        ets::SystemSummaryProfileScope scope {
            record_ids[local_system],
            1,
            system_id,
            nullptr,
            "benchmark_system",
            "profiling_benchmark.cpp",
            "benchmark_system",
            1,
        };
    }
}

double measure(
    const std::vector<ThreadRecordIds>& records,
    std::size_t scopes_per_thread
) {
    const auto threads = records.size();
    std::barrier start_line {static_cast<std::ptrdiff_t>(threads + 1)};
    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (std::size_t thread_index = 0; thread_index < threads; ++thread_index) {
        workers.emplace_back([&, thread_index]() {
            start_line.arrive_and_wait();
            run_scopes(thread_index, records[thread_index], scopes_per_thread);
        });
    }

    const auto start = std::chrono::steady_clock::now();
    start_line.arrive_and_wait();
    for (auto& worker : workers) {
        worker.join();
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    return std::chrono::duration<double, std::nano>(elapsed).count();
}

void print_result(
    std::string_view mode,
    std::size_t threads,
    std::size_t scopes_per_thread,
    double elapsed_ns
) {
    const auto total_scopes = threads * scopes_per_thread;
    std::cout << mode << ',' << threads << ',' << total_scopes << ','
              << elapsed_ns / 1'000'000.0 << ','
              << elapsed_ns / static_cast<double>(total_scopes) << '\n';
}

} // namespace

int main(int argc, char** argv) {
    const auto options = parse_options(argc, argv);
    constexpr std::size_t warmup_scopes = 4'096;
    const auto records = register_records(options.threads);

    std::cout << "mode,threads,scopes,total_ms,wall_ns_per_scope\n";

    ets::stop_profile_capture();
    run_scopes(0, records[0], warmup_scopes);
    print_result(
        "idle",
        options.threads,
        options.scopes_per_thread,
        measure(records, options.scopes_per_thread)
    );

    ets::start_profile_capture();
    for (std::size_t thread = 0; thread < options.threads; ++thread) {
        run_scopes(thread, records[thread], warmup_scopes);
    }
    print_result(
        "recording",
        options.threads,
        options.scopes_per_thread,
        measure(records, options.scopes_per_thread)
    );
    ets::stop_profile_capture();
}
