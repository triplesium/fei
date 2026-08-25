#include "ecs/dynamic/query.hpp"
#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"
#include "scripting/source.hpp"
#include "scripting_luau/compiler.hpp"
#include "scripting_luau/runtime.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace ets;

namespace {

using Clock = std::chrono::steady_clock;

struct QueryBenchmarkVector {
    double x {0.0};
    double y {0.0};
};

struct QueryBenchmarkComponent {
    QueryBenchmarkVector position;
    double x {0.0};
};

struct Options {
    std::size_t entities {10'000};
    std::size_t samples {7};
    std::chrono::milliseconds sample_time {20};
    bool csv {false};
    bool help {false};
};

struct Measurement {
    std::string name;
    std::size_t rows_per_call {0};
    std::size_t calls_per_sample {0};
    double nanoseconds_per_call {0.0};
};

void consume(std::uint64_t value) {
    static std::atomic<std::uint64_t> sink {0};
    sink.store(value, std::memory_order_relaxed);
}

std::size_t parse_size(std::string_view value, std::string_view option) {
    std::size_t result = 0;
    const auto [end, error] =
        std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc {} || end != value.data() + value.size() ||
        result == 0) {
        throw std::runtime_error(
            std::string(option) + " expects a positive integer"
        );
    }
    return result;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument {argv[index]};
        if (argument == "--csv") {
            options.csv = true;
        } else if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (argument.starts_with("--entities=")) {
            options.entities = parse_size(
                argument.substr(std::string_view("--entities=").size()),
                "--entities"
            );
        } else if (argument.starts_with("--samples=")) {
            options.samples = parse_size(
                argument.substr(std::string_view("--samples=").size()),
                "--samples"
            );
        } else if (argument.starts_with("--sample-ms=")) {
            options.sample_time = std::chrono::milliseconds(parse_size(
                argument.substr(std::string_view("--sample-ms=").size()),
                "--sample-ms"
            ));
        } else {
            throw std::runtime_error(
                "unknown benchmark option '" + std::string(argument) + "'"
            );
        }
    }
    return options;
}

void print_usage() {
    std::cout
        << "Luau ECS query benchmark\n\n"
        << "Options:\n"
        << "  --entities=N    entities with one reflected component (default "
           "10000)\n"
        << "  --samples=N     timed samples per case (default 7)\n"
        << "  --sample-ms=N   minimum duration per sample (default 20)\n"
        << "  --csv            emit machine-readable results\n";
}

template<typename Function>
std::pair<std::chrono::nanoseconds, std::uint64_t>
run_batch(std::size_t calls, Function& function) {
    std::uint64_t checksum = 0;
    const auto start = Clock::now();
    for (std::size_t call = 0; call < calls; ++call) {
        checksum ^= function() + call;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - start
    );
    consume(checksum);
    return {elapsed, checksum};
}

template<typename Function>
Measurement measure(
    std::string name,
    std::size_t rows_per_call,
    const Options& options,
    Function function
) {
    consume(function());

    constexpr std::size_t max_calls_per_sample = 1U << 20U;
    const auto target = std::chrono::duration_cast<std::chrono::nanoseconds>(
        options.sample_time
    );
    std::size_t calls = 1;
    while (calls < max_calls_per_sample) {
        const auto [elapsed, checksum] = run_batch(calls, function);
        consume(checksum);
        if (elapsed >= target) {
            break;
        }
        calls = std::min(calls * 2, max_calls_per_sample);
    }

    std::vector<double> sample_nanoseconds;
    sample_nanoseconds.reserve(options.samples);
    for (std::size_t sample = 0; sample < options.samples; ++sample) {
        const auto [elapsed, checksum] = run_batch(calls, function);
        consume(checksum);
        sample_nanoseconds.push_back(
            static_cast<double>(elapsed.count()) / static_cast<double>(calls)
        );
    }
    std::ranges::sort(sample_nanoseconds);
    const std::size_t middle = sample_nanoseconds.size() / 2;
    const double median =
        sample_nanoseconds.size() % 2 == 0 ?
            (sample_nanoseconds[middle - 1] + sample_nanoseconds[middle]) /
                2.0 :
            sample_nanoseconds[middle];
    return Measurement {
        .name = std::move(name),
        .rows_per_call = rows_per_call,
        .calls_per_sample = calls,
        .nanoseconds_per_call = median,
    };
}

std::string benchmark_source(std::size_t entities) {
    return R"(
        local sink = 0
        local entity_count = )" +
           std::to_string(entities) + R"(

        local function empty()
            sink += 1
        end

        local function pure_loop()
            local total = 0
            for index = 1, entity_count do
                total += index
            end
            sink = total
        end

        local function iterate_entities(entities: Query<Entity>)
            local total = 0
            for entity in entities do
                total += entity
            end
            sink = total
        end

        local function bridge_components(
            components: Query<Read<QueryBenchmarkComponent>>
        )
            local total = 0
            for _component in components do
                total += 1
            end
            sink = total
        end

        local function read_property_once(
            components: Query<Read<QueryBenchmarkComponent>>
        )
            local total = 0
            for component in components do
                total += component.x
            end
            sink = total
        end

        local function read_property_four_times(
            components: Query<Read<QueryBenchmarkComponent>>
        )
            local total = 0
            for component in components do
                total += component.x
                total += component.x
                total += component.x
                total += component.x
            end
            sink = total
        end

        local function write_property_once(
            components: Query<Write<QueryBenchmarkComponent>>
        )
            local total = 0
            for component in components do
                component.x = 1
                total += 1
            end
            sink = total
        end

        local function read_nested_property_once(
            components: Query<Read<QueryBenchmarkComponent>>
        )
            local total = 0
            for component in components do
                total += component.position.x
            end
            sink = total
        end

        local function read_nested_property_four_times(
            components: Query<Read<QueryBenchmarkComponent>>
        )
            local total = 0
            for component in components do
                total += component.position.x
                total += component.position.x
                total += component.position.x
                total += component.position.x
            end
            sink = total
        end

        local function read_cached_nested_property_four_times(
            components: Query<Read<QueryBenchmarkComponent>>
        )
            local total = 0
            for component in components do
                local position = component.position
                total += position.x
                total += position.x
                total += position.x
                total += position.x
            end
            sink = total
        end

        local function write_nested_property_once(
            components: Query<Write<QueryBenchmarkComponent>>
        )
            local total = 0
            for component in components do
                component.position.x = 1
                total += 1
            end
            sink = total
        end

        export local QueryBenchmarkPlugin = Plugin.new {
            build = function(app: App)
                app:add_systems(
                    Update,
                    empty,
                    pure_loop,
                    iterate_entities,
                    bridge_components,
                    read_property_once,
                    read_property_four_times,
                    write_property_once,
                    read_nested_property_once,
                    read_nested_property_four_times,
                    read_cached_nested_property_four_times,
                    write_nested_property_once
                )
            end,
        }
    )";
}

LuauScriptModuleId
load_benchmark_module(LuauRuntime& runtime, std::size_t entities) {
    const ScriptSource source {
        .name = "query_benchmark.luau",
        .content = benchmark_source(entities),
    };
    auto artifact = compile_luau_script_module(
        source,
        LuauCompileOptions {.snapshot_safe = false}
    );
    if (!artifact) {
        throw std::runtime_error(
            "failed to compile benchmark module: " + artifact.error().message
        );
    }
    auto module = runtime.load_module(*artifact);
    if (!module) {
        throw std::runtime_error(
            "failed to load benchmark module: " + module.error().message
        );
    }
    auto bound = runtime.bind_module_type(
        *module,
        "QueryBenchmarkComponent",
        type<QueryBenchmarkComponent>()
    );
    if (!bound) {
        throw std::runtime_error(
            "failed to bind benchmark component: " + bound.error().message
        );
    }
    return *module;
}

Ref prepare_query(DynamicQuery& query, World& world, SystemTicks system_ticks) {
    auto prepared = query.prepare(world, system_ticks);
    if (!prepared) {
        throw std::runtime_error(
            "failed to prepare query: " + prepared.error().message
        );
    }
    return *prepared;
}

void call_module(
    LuauRuntime& runtime,
    LuauScriptModuleId module,
    const std::string& function,
    std::span<const Ref> arguments = {}
) {
    auto status = runtime.call_module_function(module, function, arguments);
    if (!status) {
        throw std::runtime_error(
            "benchmark function '" + function +
            "' failed: " + status.error().message
        );
    }
}

std::vector<Measurement> run_benchmarks(const Options& options) {
    Registry::instance()
        .register_cls<QueryBenchmarkVector>()
        .add_property("x", &QueryBenchmarkVector::x)
        .add_property("y", &QueryBenchmarkVector::y);
    Registry::instance()
        .register_cls<QueryBenchmarkComponent>()
        .add_property("position", &QueryBenchmarkComponent::position)
        .add_property("x", &QueryBenchmarkComponent::x);

    World world;
    for (std::size_t index = 0; index < options.entities; ++index) {
        const Entity entity = world.entity();
        world.add_component(
            entity,
            QueryBenchmarkComponent {
                .position =
                    QueryBenchmarkVector {
                        .x = static_cast<double>(index + 1),
                        .y = static_cast<double>(index + 2),
                    },
                .x = static_cast<double>(index + 1),
            }
        );
    }

    DynamicQuery entity_query(
        "entities",
        {DynamicQueryField {
            .name = "entity",
            .kind = DynamicQueryFieldKind::Entity,
        }},
        {}
    );
    DynamicQuery read_query(
        "components",
        {DynamicQueryField {
            .name = "component",
            .type = type_id<QueryBenchmarkComponent>(),
            .access = DynamicParamAccess::Read,
        }},
        {}
    );
    DynamicQuery write_query(
        "components",
        {DynamicQueryField {
            .name = "component",
            .type = type_id<QueryBenchmarkComponent>(),
            .access = DynamicParamAccess::Write,
        }},
        {}
    );
    DynamicQuery prepare_only_query(
        "prepare",
        {DynamicQueryField {
            .name = "component",
            .type = type_id<QueryBenchmarkComponent>(),
            .access = DynamicParamAccess::Read,
        }},
        {}
    );

    const SystemTicks ticks {
        .last_run = 0,
        .this_run = world.increment_change_tick(),
    };
    const Ref entity_query_ref = prepare_query(entity_query, world, ticks);
    const Ref read_query_ref = prepare_query(read_query, world, ticks);
    const Ref write_query_ref = prepare_query(write_query, world, ticks);
    const std::array entity_arguments {entity_query_ref};
    const std::array read_arguments {read_query_ref};
    const std::array write_arguments {write_query_ref};

    LuauRuntime runtime;
    const LuauScriptModuleId module =
        load_benchmark_module(runtime, options.entities);

    std::vector<Measurement> results;
    results.reserve(14);
    results.push_back(measure("runtime/empty call", 0, options, [&] {
        call_module(runtime, module, "empty");
        return std::uint64_t {1};
    }));
    results.push_back(measure("query/prepare", 0, options, [&] {
        prepare_query(prepare_only_query, world, ticks);
        return std::uint64_t {1};
    }));
    results.push_back(
        measure("native/iterate rows", options.entities, options, [&] {
            DynamicQueryCursor cursor;
            DynamicQueryRow row;
            std::uint64_t total = 0;
            while (read_query.next(cursor, row)) {
                total += row.row + 1;
            }
            return total;
        })
    );
    results.push_back(
        measure("native/field + read x", options.entities, options, [&] {
            DynamicQueryCursor cursor;
            DynamicQueryRow row;
            double total = 0.0;
            while (read_query.next(cursor, row)) {
                total += read_query.field(row, 0)
                             .get_const<QueryBenchmarkComponent>()
                             .x;
            }
            return static_cast<std::uint64_t>(total);
        })
    );
    results.push_back(
        measure("luau/pure numeric loop", options.entities, options, [&] {
            call_module(runtime, module, "pure_loop");
            return std::uint64_t {1};
        })
    );
    results.push_back(
        measure("luau/query Entity", options.entities, options, [&] {
            call_module(runtime, module, "iterate_entities", entity_arguments);
            return std::uint64_t {1};
        })
    );
    results.push_back(
        measure("luau/query component bridge", options.entities, options, [&] {
            call_module(runtime, module, "bridge_components", read_arguments);
            return std::uint64_t {1};
        })
    );
    results.push_back(
        measure("luau/read property x1", options.entities, options, [&] {
            call_module(runtime, module, "read_property_once", read_arguments);
            return std::uint64_t {1};
        })
    );
    results.push_back(
        measure("luau/read property x4", options.entities, options, [&] {
            call_module(
                runtime,
                module,
                "read_property_four_times",
                read_arguments
            );
            return std::uint64_t {1};
        })
    );
    results.push_back(
        measure("luau/write property x1", options.entities, options, [&] {
            call_module(
                runtime,
                module,
                "write_property_once",
                write_arguments
            );
            return std::uint64_t {1};
        })
    );
    results.push_back(
        measure("luau/read nested property x1", options.entities, options, [&] {
            call_module(
                runtime,
                module,
                "read_nested_property_once",
                read_arguments
            );
            return std::uint64_t {1};
        })
    );
    results.push_back(
        measure("luau/read nested property x4", options.entities, options, [&] {
            call_module(
                runtime,
                module,
                "read_nested_property_four_times",
                read_arguments
            );
            return std::uint64_t {1};
        })
    );
    results.push_back(
        measure("luau/read cached nested x4", options.entities, options, [&] {
            call_module(
                runtime,
                module,
                "read_cached_nested_property_four_times",
                read_arguments
            );
            return std::uint64_t {1};
        })
    );
    results.push_back(measure(
        "luau/write nested property x1",
        options.entities,
        options,
        [&] {
            call_module(
                runtime,
                module,
                "write_nested_property_once",
                write_arguments
            );
            return std::uint64_t {1};
        }
    ));
    return results;
}

void print_results(
    const Options& options,
    const std::vector<Measurement>& results
) {
    if (options.csv) {
        std::cout << "name,entities,calls_per_sample,median_ns_per_call,median_"
                     "ns_per_row\n";
        for (const auto& result : results) {
            std::cout << result.name << ',' << options.entities << ','
                      << result.calls_per_sample << ',' << std::fixed
                      << std::setprecision(2) << result.nanoseconds_per_call
                      << ',';
            if (result.rows_per_call != 0) {
                std::cout << result.nanoseconds_per_call /
                                 static_cast<double>(result.rows_per_call);
            }
            std::cout << '\n';
        }
        return;
    }

    std::cout << "Luau ECS query benchmark (" << options.entities
              << " rows, median of " << options.samples << " samples)\n\n";
    std::cout << std::left << std::setw(36) << "case" << std::right
              << std::setw(16) << "ns/call" << std::setw(16) << "ns/row"
              << std::setw(12) << "calls" << '\n';
    std::cout << std::string(80, '-') << '\n';
    for (const auto& result : results) {
        std::cout << std::left << std::setw(36) << result.name << std::right
                  << std::setw(16) << std::fixed << std::setprecision(2)
                  << result.nanoseconds_per_call;
        if (result.rows_per_call == 0) {
            std::cout << std::setw(16) << '-';
        } else {
            std::cout << std::setw(16)
                      << result.nanoseconds_per_call /
                             static_cast<double>(result.rows_per_call);
        }
        std::cout << std::setw(12) << result.calls_per_sample << '\n';
    }
    std::cout
        << "\nQueries are prepared once for row cases. query/prepare isolates "
           "the per-system\nprepare cost. component bridge isolates borrowed "
           "userdata creation; property\ncases add reflected "
           "__index/__newindex "
           "access on top.\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        if (options.help) {
            print_usage();
            return 0;
        }
        const auto results = run_benchmarks(options);
        print_results(options, results);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "query benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
