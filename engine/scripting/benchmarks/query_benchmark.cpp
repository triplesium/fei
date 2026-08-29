#include "ecs/dynamic/query.hpp"
#include "ecs/world.hpp"
#include "lua.h"
#include "lualib.h"
#include "Luau/Common.h"
#include "Luau/Compiler.h"
#include "refl/cls.hpp"
#include "refl/property.hpp"
#include "refl/registry.hpp"
#include "scripting/source.hpp"
#include "scripting/compiler.hpp"
#include "scripting/runtime.hpp"

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
#include <unordered_map>
#include <utility>
#include <vector>

using namespace ets;

LUAU_FASTFLAG(LuauDirectFieldGet)

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

constexpr std::size_t c_flat_position_x_offset =
    offsetof(QueryBenchmarkComponent, position) +
    offsetof(QueryBenchmarkVector, x);

constexpr int c_raw_object_tag = 16;
constexpr int c_raw_vector_tag = 17;
constexpr int c_raw_direct_access_tag = 18;
constexpr int c_raw_direct_field_tag = 19;
constexpr int c_raw_dense_direct_field_tag = 20;
constexpr int16_t c_raw_flat_atom = 1;

struct RawObject {
    QueryBenchmarkComponent* component {nullptr};
};

struct RawVector {
    QueryBenchmarkVector* vector {nullptr};
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

struct ComparisonBaselines {
    const Measurement* direct {nullptr};
    const Measurement* reflection {nullptr};
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

const Measurement* find_measurement(
    const std::vector<Measurement>& results,
    const std::string& name
) {
    const auto found = std::ranges::find(results, name, &Measurement::name);
    return found == results.end() ? nullptr : &*found;
}

ComparisonBaselines comparison_baselines(
    const std::vector<Measurement>& results,
    const Measurement& result
) {
    constexpr std::string_view luau_prefix = "luau/";
    constexpr std::string_view reflection_prefix = "cpp/reflection ";
    std::string_view suffix;
    const bool is_luau = result.name.starts_with(luau_prefix);
    if (is_luau) {
        suffix = std::string_view {result.name}.substr(luau_prefix.size());
    } else if (result.name.starts_with(reflection_prefix)) {
        suffix =
            std::string_view {result.name}.substr(reflection_prefix.size());
    } else {
        return {};
    }

    ComparisonBaselines baselines;
    baselines.direct =
        find_measurement(results, "cpp/direct " + std::string {suffix});
    if (is_luau) {
        baselines.reflection =
            find_measurement(results, "cpp/reflection " + std::string {suffix});
    }
    return baselines;
}

int raw_vector_index(lua_State* state) {
    auto* object = static_cast<RawVector*>(
        luaL_checkudatatagged(state, 1, c_raw_vector_tag)
    );
    const std::string_view key {luaL_checkstring(state, 2)};
    if (key == "x") {
        lua_pushnumber(state, object->vector->x);
        return 1;
    }
    luaL_error(
        state,
        "unknown raw vector field '%.*s'",
        static_cast<int>(key.size()),
        key.data()
    );
}

int raw_vector_newindex(lua_State* state) {
    auto* object = static_cast<RawVector*>(
        luaL_checkudatatagged(state, 1, c_raw_vector_tag)
    );
    const std::string_view key {luaL_checkstring(state, 2)};
    if (key == "x") {
        object->vector->x = luaL_checknumber(state, 3);
        return 0;
    }
    luaL_error(
        state,
        "unknown raw vector field '%.*s'",
        static_cast<int>(key.size()),
        key.data()
    );
}

int raw_object_index(lua_State* state) {
    auto* object = static_cast<RawObject*>(
        luaL_checkudatatagged(state, 1, c_raw_object_tag)
    );
    const std::string_view key {luaL_checkstring(state, 2)};
    if (key == "position") {
        new (lua_newuserdatataggedwithmetatable(
            state,
            sizeof(RawVector),
            c_raw_vector_tag
        )) RawVector {&object->component->position};
        return 1;
    }
    if (key == "__f0") {
        lua_pushnumber(state, object->component->position.x);
        return 1;
    }
    luaL_error(
        state,
        "unknown raw object field '%.*s'",
        static_cast<int>(key.size()),
        key.data()
    );
}

int raw_object_newindex(lua_State* state) {
    auto* object = static_cast<RawObject*>(
        luaL_checkudatatagged(state, 1, c_raw_object_tag)
    );
    const std::string_view key {luaL_checkstring(state, 2)};
    if (key == "__f0") {
        object->component->position.x = luaL_checknumber(state, 3);
        return 0;
    }
    luaL_error(
        state,
        "unknown raw object field '%.*s'",
        static_cast<int>(key.size()),
        key.data()
    );
}

int raw_direct_fallback(lua_State* state) {
    luaL_error(
        state,
        "raw direct userdata unexpectedly used its metamethod fallback"
    );
}

void raw_direct_access_get(
    lua_State* state,
    void* data,
    int atom,
    uint16_t* cachedslot,
    int
) {
    if (*cachedslot == 0 && atom == c_raw_flat_atom) {
        *cachedslot = 1;
    }
    if (*cachedslot != 1) {
        luaL_error(state, "unknown raw direct-access atom %d", atom);
    }
    const auto* object = static_cast<const RawObject*>(data);
    lua_pushnumber(state, object->component->position.x);
}

void raw_direct_access_set(
    lua_State* state,
    void* data,
    int atom,
    uint16_t* cachedslot,
    int
) {
    if (*cachedslot == 0 && atom == c_raw_flat_atom) {
        *cachedslot = 1;
    }
    if (*cachedslot != 1) {
        luaL_error(state, "unknown raw direct-access atom %d", atom);
    }
    auto* object = static_cast<RawObject*>(data);
    object->component->position.x = luaL_checknumber(state, 3);
}

void raw_direct_field_get(void* data, void* result) {
    const auto* object = static_cast<const RawObject*>(data);
    lua_userdatadirectfield_setnumber(result, object->component->position.x);
}

int raw_path_get(lua_State* state) {
    const auto* object = static_cast<const RawObject*>(
        luaL_checkudatatagged(state, 1, c_raw_object_tag)
    );
    lua_pushnumber(state, object->component->position.x);
    return 1;
}

int raw_path_set(lua_State* state) {
    auto* object = static_cast<RawObject*>(
        luaL_checkudatatagged(state, 1, c_raw_object_tag)
    );
    object->component->position.x = luaL_checknumber(state, 2);
    return 0;
}

void install_raw_metatable(
    lua_State* state,
    int tag,
    lua_CFunction index,
    lua_CFunction newindex = nullptr
) {
    lua_newtable(state);
    lua_pushcfunction(state, index, "raw.__index");
    lua_setfield(state, -2, "__index");
    if (newindex != nullptr) {
        lua_pushcfunction(state, newindex, "raw.__newindex");
        lua_setfield(state, -2, "__newindex");
    }
    lua_setuserdatametatable(state, tag);
}

class RawLuauDispatchBenchmark {
  private:
    lua_State* m_state {nullptr};
    QueryBenchmarkComponent m_component {
        .position = QueryBenchmarkVector {.x = 1.0, .y = 2.0},
        .x = 1.0,
    };
    std::unordered_map<std::string, int> m_functions;

    void push_object_global(int tag, const char* name) {
        new (
            lua_newuserdatataggedwithmetatable(m_state, sizeof(RawObject), tag)
        ) RawObject {&m_component};
        lua_setglobal(m_state, name);
    }

    void load(std::size_t iterations) {
        const std::string source = R"(
            local iterations = )" + std::to_string(iterations) +
                                   R"(
            local nested = raw_nested_object
            local flat_index = raw_flat_index_object
            local direct_access = raw_direct_access_object
            local direct_field = raw_direct_field_object
            local dense_direct_field = raw_dense_direct_field_object
            local get_path = __ets_get_path
            local set_path = __ets_set_path
            local native_table = { value = 1 }
            local write_value = 1

            return {
                native_table_x1 = function()
                    local total = 0
                    for _ = 1, iterations do
                        total += native_table.value
                    end
                    return total
                end,
                native_table_x4 = function()
                    local total = 0
                    for _ = 1, iterations do
                        total += native_table.value
                        total += native_table.value
                        total += native_table.value
                        total += native_table.value
                    end
                    return total
                end,
                nested_x1 = function()
                    local total = 0
                    for _ = 1, iterations do
                        total += nested.position.x
                    end
                    return total
                end,
                nested_x4 = function()
                    local total = 0
                    for _ = 1, iterations do
                        total += nested.position.x
                        total += nested.position.x
                        total += nested.position.x
                        total += nested.position.x
                    end
                    return total
                end,
                cached_nested_x4 = function()
                    local total = 0
                    for _ = 1, iterations do
                        local position = nested.position
                        total += position.x
                        total += position.x
                        total += position.x
                        total += position.x
                    end
                    return total
                end,
                nested_write_x1 = function()
                    write_value = 3 - write_value
                    for _ = 1, iterations do
                        nested.position.x = write_value
                    end
                    return nested.position.x
                end,
                flat_index_x1 = function()
                    local total = 0
                    for _ = 1, iterations do
                        total += flat_index.__f0
                    end
                    return total
                end,
                flat_index_x4 = function()
                    local total = 0
                    for _ = 1, iterations do
                        total += flat_index.__f0
                        total += flat_index.__f0
                        total += flat_index.__f0
                        total += flat_index.__f0
                    end
                    return total
                end,
                flat_index_write_x1 = function()
                    write_value = 3 - write_value
                    for _ = 1, iterations do
                        flat_index.__f0 = write_value
                    end
                    return flat_index.__f0
                end,
                path_function_x1 = function()
                    local total = 0
                    for _ = 1, iterations do
                        total += get_path(flat_index)
                    end
                    return total
                end,
                path_function_x4 = function()
                    local total = 0
                    for _ = 1, iterations do
                        total += get_path(flat_index)
                        total += get_path(flat_index)
                        total += get_path(flat_index)
                        total += get_path(flat_index)
                    end
                    return total
                end,
                path_function_write_x1 = function()
                    write_value = 3 - write_value
                    for _ = 1, iterations do
                        set_path(flat_index, write_value)
                    end
                    return get_path(flat_index)
                end,
                direct_access_x1 = function()
                    local total = 0
                    for _ = 1, iterations do
                        total += direct_access.__fa
                    end
                    return total
                end,
                direct_access_x4 = function()
                    local total = 0
                    for _ = 1, iterations do
                        total += direct_access.__fa
                        total += direct_access.__fa
                        total += direct_access.__fa
                        total += direct_access.__fa
                    end
                    return total
                end,
                direct_access_write_x1 = function()
                    write_value = 3 - write_value
                    for _ = 1, iterations do
                        direct_access.__fa = write_value
                    end
                    return direct_access.__fa
                end,
                direct_field_x1 = function()
                    local total = 0
                    for _ = 1, iterations do
                        total += direct_field.__ff
                    end
                    return total
                end,
                direct_field_x4 = function()
                    local total = 0
                    for _ = 1, iterations do
                        total += direct_field.__ff
                        total += direct_field.__ff
                        total += direct_field.__ff
                        total += direct_field.__ff
                    end
                    return total
                end,
                dense_direct_field_x1 = function()
                    local total = 0
                    for _ = 1, iterations do
                        total += dense_direct_field.__ffd
                    end
                    return total
                end,
                dense_direct_field_x4 = function()
                    local total = 0
                    for _ = 1, iterations do
                        total += dense_direct_field.__ffd
                        total += dense_direct_field.__ffd
                        total += dense_direct_field.__ffd
                        total += dense_direct_field.__ffd
                    end
                    return total
                end,
            }
        )";
        const std::string bytecode = Luau::compile(source);
        if (luau_load(
                m_state,
                "raw_dispatch_benchmark",
                bytecode.data(),
                bytecode.size(),
                0
            ) != LUA_OK) {
            throw std::runtime_error(
                "failed to load raw dispatch benchmark: " +
                std::string {lua_tostring(m_state, -1)}
            );
        }
        if (lua_pcall(m_state, 0, 1, 0) != LUA_OK) {
            throw std::runtime_error(
                "failed to initialize raw dispatch benchmark: " +
                std::string {lua_tostring(m_state, -1)}
            );
        }

        constexpr std::array names {
            "native_table_x1",
            "native_table_x4",
            "nested_x1",
            "nested_x4",
            "cached_nested_x4",
            "nested_write_x1",
            "flat_index_x1",
            "flat_index_x4",
            "flat_index_write_x1",
            "path_function_x1",
            "path_function_x4",
            "path_function_write_x1",
            "direct_access_x1",
            "direct_access_x4",
            "direct_access_write_x1",
            "direct_field_x1",
            "direct_field_x4",
            "dense_direct_field_x1",
            "dense_direct_field_x4",
        };
        for (const char* name : names) {
            lua_getfield(m_state, -1, name);
            if (!lua_isfunction(m_state, -1)) {
                throw std::runtime_error(
                    "raw dispatch benchmark is missing function '" +
                    std::string {name} + "'"
                );
            }
            m_functions.emplace(name, lua_ref(m_state, -1));
            lua_pop(m_state, 1);
        }
        lua_pop(m_state, 1);
    }

  public:
    explicit RawLuauDispatchBenchmark(std::size_t iterations) :
        m_state(luaL_newstate()) {
        if (m_state == nullptr) {
            throw std::runtime_error("failed to create raw Luau benchmark VM");
        }
        FFlag::LuauDirectFieldGet.value = true;
        luaL_openlibs(m_state);

        install_raw_metatable(
            m_state,
            c_raw_object_tag,
            raw_object_index,
            raw_object_newindex
        );
        install_raw_metatable(
            m_state,
            c_raw_vector_tag,
            raw_vector_index,
            raw_vector_newindex
        );
        install_raw_metatable(
            m_state,
            c_raw_direct_access_tag,
            raw_direct_fallback,
            raw_direct_fallback
        );
        install_raw_metatable(
            m_state,
            c_raw_direct_field_tag,
            raw_direct_fallback
        );
        install_raw_metatable(
            m_state,
            c_raw_dense_direct_field_tag,
            raw_direct_fallback
        );

        if (lua_registeruserdatadirectaccess(
                m_state,
                c_raw_direct_access_tag,
                raw_direct_access_get,
                raw_direct_access_set,
                nullptr
            ) == 0) {
            throw std::runtime_error(
                "failed to register raw direct-access benchmark"
            );
        }
        lua_registeruserdatadirectfieldget(
            m_state,
            c_raw_direct_field_tag,
            "__ff",
            raw_direct_field_get
        );
        for (std::size_t index = 0; index < 1024; ++index) {
            const std::string name = "__unused_" + std::to_string(index);
            lua_registeruserdatadirectfieldget(
                m_state,
                c_raw_dense_direct_field_tag,
                name.c_str(),
                raw_direct_field_get
            );
        }
        lua_registeruserdatadirectfieldget(
            m_state,
            c_raw_dense_direct_field_tag,
            "__ffd",
            raw_direct_field_get
        );

        push_object_global(c_raw_object_tag, "raw_nested_object");
        push_object_global(c_raw_object_tag, "raw_flat_index_object");
        push_object_global(c_raw_direct_access_tag, "raw_direct_access_object");
        push_object_global(c_raw_direct_field_tag, "raw_direct_field_object");
        push_object_global(
            c_raw_dense_direct_field_tag,
            "raw_dense_direct_field_object"
        );
        lua_pushcfunction(m_state, raw_path_get, "__ets_get_path");
        lua_setglobal(m_state, "__ets_get_path");
        lua_pushcfunction(m_state, raw_path_set, "__ets_set_path");
        lua_setglobal(m_state, "__ets_set_path");

        lua_callbacks(m_state)->useratom =
            [](lua_State*, const char* text, std::size_t length) -> int16_t {
            return std::string_view {text, length} == "__fa" ? c_raw_flat_atom :
                                                               -1;
        };
        load(iterations);
    }

    ~RawLuauDispatchBenchmark() {
        if (m_state != nullptr) {
            lua_close(m_state);
        }
    }

    RawLuauDispatchBenchmark(const RawLuauDispatchBenchmark&) = delete;
    RawLuauDispatchBenchmark&
    operator=(const RawLuauDispatchBenchmark&) = delete;

    int function(std::string_view name) const {
        return m_functions.at(std::string {name});
    }

    std::uint64_t call(int function_ref) {
        lua_getref(m_state, function_ref);
        if (lua_pcall(m_state, 0, 1, 0) != LUA_OK) {
            const std::string error {lua_tostring(m_state, -1)};
            lua_pop(m_state, 1);
            throw std::runtime_error(
                "raw dispatch benchmark call failed: " + error
            );
        }
        const auto result =
            static_cast<std::uint64_t>(lua_tonumber(m_state, -1));
        lua_pop(m_state, 1);
        return result;
    }
};

std::string benchmark_source(std::size_t entities) {
    return R"(
        local sink = 0
        local write_value = 1
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
            write_value = 3 - write_value
            local total = 0
            for component in components do
                component.x = write_value
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

        local function read_flattened_offset_once(
            components: Query<Read<QueryBenchmarkComponent>>
        )
            local total = 0
            for component in components do
                total += component.__ets_f0
            end
            sink = total
        end

        local function read_flattened_offset_four_times(
            components: Query<Read<QueryBenchmarkComponent>>
        )
            local total = 0
            for component in components do
                total += component.__ets_f0
                total += component.__ets_f0
                total += component.__ets_f0
                total += component.__ets_f0
            end
            sink = total
        end

        local function write_nested_property_once(
            components: Query<Write<QueryBenchmarkComponent>>
        )
            write_value = 3 - write_value
            local total = 0
            for component in components do
                component.position.x = write_value
                total += 1
            end
            sink = total
        end

        local function write_cached_nested_property_once(
            components: Query<Write<QueryBenchmarkComponent>>
        )
            write_value = 3 - write_value
            local total = 0
            for component in components do
                local position = component.position
                position.x = write_value
                total += 1
            end
            sink = total
        end

        local function write_flattened_offset_once(
            components: Query<Write<QueryBenchmarkComponent>>
        )
            write_value = 3 - write_value
            local total = 0
            for component in components do
                component.__ets_f0 = write_value
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
                    write_nested_property_once,
                    write_cached_nested_property_once,
                    read_flattened_offset_once,
                    read_flattened_offset_four_times,
                    write_flattened_offset_once
                )
            end,
        }
    )";
}

LuauScriptModuleId
load_benchmark_module(LuauRuntime& runtime, std::size_t entities) {
    const LuauScriptSource source {
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

Property& required_property(TypeId type, std::string_view name) {
    auto cls = Registry::instance().try_get_cls(type);
    if (!cls) {
        throw std::runtime_error(
            "benchmark reflected class lookup failed: " + cls.error().message
        );
    }
    auto property = cls->try_get_property(std::string {name});
    if (!property) {
        throw std::runtime_error(
            "benchmark reflected property lookup failed: " +
            property.error().message
        );
    }
    return *property;
}

Ref get_property(Property& property, Ref object) {
    auto value = property.get(object);
    if (!value) {
        throw std::runtime_error(
            "benchmark reflected property get failed: " + value.error().message
        );
    }
    return *value;
}

bool set_double_property(Property& property, Ref object, double value) {
    auto current = get_property(property, object);
    if (current.get_const<double>() == value) {
        return false;
    }
    auto status = property.set(object, Ref(value));
    if (!status) {
        throw std::runtime_error(
            "benchmark reflected property set failed: " + status.error().message
        );
    }
    return true;
}

void mark_changed(const DynamicQueryFieldBorrow& field) {
    if (field.ticks != nullptr) {
        field.ticks->mark_changed(field.change_tick);
    }
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
        .add_property("x", &QueryBenchmarkComponent::x)
        .add_offset_property(
            "__ets_f0",
            type_id<double>(),
            c_flat_position_x_offset
        );
    auto& component_position =
        required_property(type_id<QueryBenchmarkComponent>(), "position");
    auto& component_x =
        required_property(type_id<QueryBenchmarkComponent>(), "x");
    auto& vector_x = required_property(type_id<QueryBenchmarkVector>(), "x");
    auto& flat_position_x =
        required_property(type_id<QueryBenchmarkComponent>(), "__ets_f0");

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
    results.reserve(56);
    results.push_back(measure("runtime/empty call", 0, options, [&] {
        call_module(runtime, module, "empty");
        return std::uint64_t {1};
    }));
    results.push_back(measure("query/prepare", 0, options, [&] {
        prepare_query(prepare_only_query, world, ticks);
        return std::uint64_t {1};
    }));
    results.push_back(measure("cpp/query rows", options.entities, options, [&] {
        DynamicQueryCursor cursor;
        DynamicQueryRow row;
        std::uint64_t total = 0;
        while (read_query.next(cursor, row)) {
            total += row.row + 1;
        }
        return total;
    }));
    results.push_back(
        measure("cpp/direct pure numeric loop", options.entities, options, [&] {
            std::uint64_t total = 0;
            for (std::size_t index = 1; index <= options.entities; ++index) {
                total += index;
            }
            return total;
        })
    );
    results.push_back(
        measure("cpp/direct query Entity", options.entities, options, [&] {
            DynamicQueryCursor cursor;
            DynamicQueryRow row;
            std::uint64_t total = 0;
            while (entity_query.next(cursor, row)) {
                total += entity_query.field_untracked(row, 0)
                             .value.get_const<Entity>()
                             .value;
            }
            return total;
        })
    );
    results.push_back(
        measure("cpp/direct read property x1", options.entities, options, [&] {
            DynamicQueryCursor cursor;
            DynamicQueryRow row;
            double total = 0.0;
            while (read_query.next(cursor, row)) {
                total += read_query.field_untracked(row, 0)
                             .value.get_const<QueryBenchmarkComponent>()
                             .x;
            }
            return static_cast<std::uint64_t>(total);
        })
    );
    results.push_back(
        measure("cpp/direct read property x4", options.entities, options, [&] {
            DynamicQueryCursor cursor;
            DynamicQueryRow row;
            double total = 0.0;
            while (read_query.next(cursor, row)) {
                const auto& component =
                    read_query.field_untracked(row, 0)
                        .value.get_const<QueryBenchmarkComponent>();
                total += component.x;
                total += component.x;
                total += component.x;
                total += component.x;
            }
            return static_cast<std::uint64_t>(total);
        })
    );
    double direct_write_value = 1.0;
    results.push_back(
        measure("cpp/direct write property x1", options.entities, options, [&] {
            direct_write_value = 3.0 - direct_write_value;
            DynamicQueryCursor cursor;
            DynamicQueryRow row;
            std::uint64_t total = 0;
            while (write_query.next(cursor, row)) {
                const auto field = write_query.field_untracked(row, 0);
                auto& component = field.value.get<QueryBenchmarkComponent>();
                if (component.x != direct_write_value) {
                    component.x = direct_write_value;
                    mark_changed(field);
                }
                ++total;
            }
            return total;
        })
    );
    results.push_back(measure(
        "cpp/direct read nested property x1",
        options.entities,
        options,
        [&] {
            DynamicQueryCursor cursor;
            DynamicQueryRow row;
            double total = 0.0;
            while (read_query.next(cursor, row)) {
                total += read_query.field_untracked(row, 0)
                             .value.get_const<QueryBenchmarkComponent>()
                             .position.x;
            }
            return static_cast<std::uint64_t>(total);
        }
    ));
    results.push_back(measure(
        "cpp/direct read nested property x4",
        options.entities,
        options,
        [&] {
            DynamicQueryCursor cursor;
            DynamicQueryRow row;
            double total = 0.0;
            while (read_query.next(cursor, row)) {
                const auto& component =
                    read_query.field_untracked(row, 0)
                        .value.get_const<QueryBenchmarkComponent>();
                total += component.position.x;
                total += component.position.x;
                total += component.position.x;
                total += component.position.x;
            }
            return static_cast<std::uint64_t>(total);
        }
    ));
    results.push_back(measure(
        "cpp/direct read cached nested x4",
        options.entities,
        options,
        [&] {
            DynamicQueryCursor cursor;
            DynamicQueryRow row;
            double total = 0.0;
            while (read_query.next(cursor, row)) {
                const auto& position =
                    read_query.field_untracked(row, 0)
                        .value.get_const<QueryBenchmarkComponent>()
                        .position;
                total += position.x;
                total += position.x;
                total += position.x;
                total += position.x;
            }
            return static_cast<std::uint64_t>(total);
        }
    ));
    double direct_nested_write_value = 1.0;
    results.push_back(measure(
        "cpp/direct write nested property x1",
        options.entities,
        options,
        [&] {
            direct_nested_write_value = 3.0 - direct_nested_write_value;
            DynamicQueryCursor cursor;
            DynamicQueryRow row;
            std::uint64_t total = 0;
            while (write_query.next(cursor, row)) {
                const auto field = write_query.field_untracked(row, 0);
                auto& position =
                    field.value.get<QueryBenchmarkComponent>().position;
                if (position.x != direct_nested_write_value) {
                    position.x = direct_nested_write_value;
                    mark_changed(field);
                }
                ++total;
            }
            return total;
        }
    ));
    results.push_back(measure(
        "cpp/reflection read property x1",
        options.entities,
        options,
        [&] {
            DynamicQueryCursor cursor;
            DynamicQueryRow row;
            double total = 0.0;
            while (read_query.next(cursor, row)) {
                const Ref component = read_query.field_untracked(row, 0).value;
                total +=
                    get_property(component_x, component).get_const<double>();
            }
            return static_cast<std::uint64_t>(total);
        }
    ));
    results.push_back(measure(
        "cpp/reflection read property x4",
        options.entities,
        options,
        [&] {
            DynamicQueryCursor cursor;
            DynamicQueryRow row;
            double total = 0.0;
            while (read_query.next(cursor, row)) {
                const Ref component = read_query.field_untracked(row, 0).value;
                total +=
                    get_property(component_x, component).get_const<double>();
                total +=
                    get_property(component_x, component).get_const<double>();
                total +=
                    get_property(component_x, component).get_const<double>();
                total +=
                    get_property(component_x, component).get_const<double>();
            }
            return static_cast<std::uint64_t>(total);
        }
    ));
    double reflected_write_value = 1.0;
    results.push_back(measure(
        "cpp/reflection write property x1",
        options.entities,
        options,
        [&] {
            reflected_write_value = 3.0 - reflected_write_value;
            DynamicQueryCursor cursor;
            DynamicQueryRow row;
            std::uint64_t total = 0;
            while (write_query.next(cursor, row)) {
                const auto field = write_query.field_untracked(row, 0);
                if (set_double_property(
                        component_x,
                        field.value,
                        reflected_write_value
                    )) {
                    mark_changed(field);
                }
                ++total;
            }
            return total;
        }
    ));
    results.push_back(measure(
        "cpp/reflection read nested property x1",
        options.entities,
        options,
        [&] {
            DynamicQueryCursor cursor;
            DynamicQueryRow row;
            double total = 0.0;
            while (read_query.next(cursor, row)) {
                const Ref component = read_query.field_untracked(row, 0).value;
                const Ref position =
                    get_property(component_position, component);
                total += get_property(vector_x, position).get_const<double>();
            }
            return static_cast<std::uint64_t>(total);
        }
    ));
    results.push_back(measure(
        "cpp/reflection read nested property x4",
        options.entities,
        options,
        [&] {
            DynamicQueryCursor cursor;
            DynamicQueryRow row;
            double total = 0.0;
            while (read_query.next(cursor, row)) {
                const Ref component = read_query.field_untracked(row, 0).value;
                total += get_property(
                             vector_x,
                             get_property(component_position, component)
                )
                             .get_const<double>();
                total += get_property(
                             vector_x,
                             get_property(component_position, component)
                )
                             .get_const<double>();
                total += get_property(
                             vector_x,
                             get_property(component_position, component)
                )
                             .get_const<double>();
                total += get_property(
                             vector_x,
                             get_property(component_position, component)
                )
                             .get_const<double>();
            }
            return static_cast<std::uint64_t>(total);
        }
    ));
    results.push_back(measure(
        "cpp/reflection read cached nested x4",
        options.entities,
        options,
        [&] {
            DynamicQueryCursor cursor;
            DynamicQueryRow row;
            double total = 0.0;
            while (read_query.next(cursor, row)) {
                const Ref component = read_query.field_untracked(row, 0).value;
                const Ref position =
                    get_property(component_position, component);
                total += get_property(vector_x, position).get_const<double>();
                total += get_property(vector_x, position).get_const<double>();
                total += get_property(vector_x, position).get_const<double>();
                total += get_property(vector_x, position).get_const<double>();
            }
            return static_cast<std::uint64_t>(total);
        }
    ));
    double reflected_nested_write_value = 1.0;
    results.push_back(measure(
        "cpp/reflection write nested property x1",
        options.entities,
        options,
        [&] {
            reflected_nested_write_value = 3.0 - reflected_nested_write_value;
            DynamicQueryCursor cursor;
            DynamicQueryRow row;
            std::uint64_t total = 0;
            while (write_query.next(cursor, row)) {
                const auto field = write_query.field_untracked(row, 0);
                const Ref position =
                    get_property(component_position, field.value);
                if (set_double_property(
                        vector_x,
                        position,
                        reflected_nested_write_value
                    )) {
                    mark_changed(field);
                }
                ++total;
            }
            return total;
        }
    ));
    results.push_back(measure(
        "cpp/reflection read flattened offset x1",
        options.entities,
        options,
        [&] {
            DynamicQueryCursor cursor;
            DynamicQueryRow row;
            double total = 0.0;
            while (read_query.next(cursor, row)) {
                const Ref component = read_query.field_untracked(row, 0).value;
                total += get_property(flat_position_x, component)
                             .get_const<double>();
            }
            return static_cast<std::uint64_t>(total);
        }
    ));
    results.push_back(measure(
        "cpp/reflection read flattened offset x4",
        options.entities,
        options,
        [&] {
            DynamicQueryCursor cursor;
            DynamicQueryRow row;
            double total = 0.0;
            while (read_query.next(cursor, row)) {
                const Ref component = read_query.field_untracked(row, 0).value;
                total += get_property(flat_position_x, component)
                             .get_const<double>();
                total += get_property(flat_position_x, component)
                             .get_const<double>();
                total += get_property(flat_position_x, component)
                             .get_const<double>();
                total += get_property(flat_position_x, component)
                             .get_const<double>();
            }
            return static_cast<std::uint64_t>(total);
        }
    ));
    double reflected_flat_write_value = 1.0;
    results.push_back(measure(
        "cpp/reflection write flattened offset x1",
        options.entities,
        options,
        [&] {
            reflected_flat_write_value = 3.0 - reflected_flat_write_value;
            DynamicQueryCursor cursor;
            DynamicQueryRow row;
            std::uint64_t total = 0;
            while (write_query.next(cursor, row)) {
                const auto field = write_query.field_untracked(row, 0);
                if (set_double_property(
                        flat_position_x,
                        field.value,
                        reflected_flat_write_value
                    )) {
                    mark_changed(field);
                }
                ++total;
            }
            return total;
        }
    ));
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
    results.push_back(
        measure("luau/write cached nested x1", options.entities, options, [&] {
            call_module(
                runtime,
                module,
                "write_cached_nested_property_once",
                write_arguments
            );
            return std::uint64_t {1};
        })
    );
    results.push_back(measure(
        "luau/read flattened offset x1",
        options.entities,
        options,
        [&] {
            call_module(
                runtime,
                module,
                "read_flattened_offset_once",
                read_arguments
            );
            return std::uint64_t {1};
        }
    ));
    results.push_back(measure(
        "luau/read flattened offset x4",
        options.entities,
        options,
        [&] {
            call_module(
                runtime,
                module,
                "read_flattened_offset_four_times",
                read_arguments
            );
            return std::uint64_t {1};
        }
    ));
    results.push_back(measure(
        "luau/write flattened offset x1",
        options.entities,
        options,
        [&] {
            call_module(
                runtime,
                module,
                "write_flattened_offset_once",
                write_arguments
            );
            return std::uint64_t {1};
        }
    ));

    RawLuauDispatchBenchmark raw_dispatch(options.entities);
    const int raw_native_table_x1 = raw_dispatch.function("native_table_x1");
    const int raw_native_table_x4 = raw_dispatch.function("native_table_x4");
    const int raw_nested_x1 = raw_dispatch.function("nested_x1");
    const int raw_nested_x4 = raw_dispatch.function("nested_x4");
    const int raw_cached_nested_x4 = raw_dispatch.function("cached_nested_x4");
    const int raw_nested_write_x1 = raw_dispatch.function("nested_write_x1");
    const int raw_flat_index_x1 = raw_dispatch.function("flat_index_x1");
    const int raw_flat_index_x4 = raw_dispatch.function("flat_index_x4");
    const int raw_flat_index_write_x1 =
        raw_dispatch.function("flat_index_write_x1");
    const int raw_path_function_x1 = raw_dispatch.function("path_function_x1");
    const int raw_path_function_x4 = raw_dispatch.function("path_function_x4");
    const int raw_path_function_write_x1 =
        raw_dispatch.function("path_function_write_x1");
    const int raw_direct_access_x1 = raw_dispatch.function("direct_access_x1");
    const int raw_direct_access_x4 = raw_dispatch.function("direct_access_x4");
    const int raw_direct_access_write_x1 =
        raw_dispatch.function("direct_access_write_x1");
    const int raw_direct_field_x1 = raw_dispatch.function("direct_field_x1");
    const int raw_direct_field_x4 = raw_dispatch.function("direct_field_x4");
    const int raw_dense_direct_field_x1 =
        raw_dispatch.function("dense_direct_field_x1");
    const int raw_dense_direct_field_x4 =
        raw_dispatch.function("dense_direct_field_x4");

    const auto add_raw_dispatch = [&](std::string name, int function_ref) {
        results.push_back(measure(
            std::move(name),
            options.entities,
            options,
            [&raw_dispatch, function_ref] {
                return raw_dispatch.call(function_ref);
            }
        ));
    };
    add_raw_dispatch("raw-vm/native table x1", raw_native_table_x1);
    add_raw_dispatch("raw-vm/native table x4", raw_native_table_x4);
    add_raw_dispatch("raw-vm/nested userdata x1", raw_nested_x1);
    add_raw_dispatch("raw-vm/nested userdata x4", raw_nested_x4);
    add_raw_dispatch("raw-vm/cached nested userdata x4", raw_cached_nested_x4);
    add_raw_dispatch("raw-vm/nested userdata write x1", raw_nested_write_x1);
    add_raw_dispatch("raw-vm/flat __index offset x1", raw_flat_index_x1);
    add_raw_dispatch("raw-vm/flat __index offset x4", raw_flat_index_x4);
    add_raw_dispatch(
        "raw-vm/flat __newindex offset x1",
        raw_flat_index_write_x1
    );
    add_raw_dispatch("raw-vm/path C function x1", raw_path_function_x1);
    add_raw_dispatch("raw-vm/path C function x4", raw_path_function_x4);
    add_raw_dispatch(
        "raw-vm/path C function write x1",
        raw_path_function_write_x1
    );
    add_raw_dispatch("raw-vm/directaccess offset x1", raw_direct_access_x1);
    add_raw_dispatch("raw-vm/directaccess offset x4", raw_direct_access_x4);
    add_raw_dispatch(
        "raw-vm/directaccess offset write x1",
        raw_direct_access_write_x1
    );
    add_raw_dispatch("raw-vm/directfield offset x1", raw_direct_field_x1);
    add_raw_dispatch("raw-vm/directfield offset x4", raw_direct_field_x4);
    add_raw_dispatch(
        "raw-vm/directfield 1025 fields x1",
        raw_dense_direct_field_x1
    );
    add_raw_dispatch(
        "raw-vm/directfield 1025 fields x4",
        raw_dense_direct_field_x4
    );
    return results;
}

void print_results(
    const Options& options,
    const std::vector<Measurement>& results
) {
    if (options.csv) {
        std::cout << "name,entities,calls_per_sample,median_ns_per_call,median_"
                     "ns_per_row,vs_cpp_direct,vs_cpp_reflection\n";
        for (const auto& result : results) {
            const auto baselines = comparison_baselines(results, result);
            std::cout << result.name << ',' << options.entities << ','
                      << result.calls_per_sample << ',' << std::fixed
                      << std::setprecision(2) << result.nanoseconds_per_call
                      << ',';
            if (result.rows_per_call != 0) {
                std::cout << result.nanoseconds_per_call /
                                 static_cast<double>(result.rows_per_call);
            }
            std::cout << ',';
            if (baselines.direct != nullptr) {
                std::cout << result.nanoseconds_per_call /
                                 baselines.direct->nanoseconds_per_call;
            }
            std::cout << ',';
            if (baselines.reflection != nullptr) {
                std::cout << result.nanoseconds_per_call /
                                 baselines.reflection->nanoseconds_per_call;
            }
            std::cout << '\n';
        }
        return;
    }

    std::cout << "Luau ECS query benchmark (" << options.entities
              << " rows, median of " << options.samples << " samples)\n\n";
    std::cout << std::left << std::setw(44) << "case" << std::right
              << std::setw(16) << "ns/call" << std::setw(16) << "ns/row"
              << std::setw(12) << "vs direct" << std::setw(16)
              << "vs reflection" << std::setw(12) << "calls" << '\n';
    std::cout << std::string(116, '-') << '\n';
    for (const auto& result : results) {
        const auto baselines = comparison_baselines(results, result);
        std::cout << std::left << std::setw(44) << result.name << std::right
                  << std::setw(16) << std::fixed << std::setprecision(2)
                  << result.nanoseconds_per_call;
        if (result.rows_per_call == 0) {
            std::cout << std::setw(16) << '-';
        } else {
            std::cout << std::setw(16)
                      << result.nanoseconds_per_call /
                             static_cast<double>(result.rows_per_call);
        }
        if (baselines.direct == nullptr) {
            std::cout << std::setw(12) << '-';
        } else {
            std::cout << std::setw(11) << std::setprecision(2)
                      << result.nanoseconds_per_call /
                             baselines.direct->nanoseconds_per_call
                      << 'x';
        }
        if (baselines.reflection == nullptr) {
            std::cout << std::setw(16) << '-';
        } else {
            std::cout << std::setw(15) << std::setprecision(2)
                      << result.nanoseconds_per_call /
                             baselines.reflection->nanoseconds_per_call
                      << 'x';
        }
        std::cout << std::setw(12) << result.calls_per_sample << '\n';
    }
    std::cout
        << "\nQueries are prepared once for row cases. query/prepare isolates "
           "the per-system\nprepare cost. Matching cpp/direct, "
           "cpp/reflection, and luau suffixes perform\nthe same work at each "
           "layer. Write cases alternate values to include mutation "
           "and\nchange-"
           "tick tracking. component bridge isolates borrowed userdata "
           "creation.\n";
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
