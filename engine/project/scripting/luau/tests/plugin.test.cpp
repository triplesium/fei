#include "project_scripting_lua/plugin.hpp"

#include "app/app.hpp"
#include "project/project.hpp"
#include "project_runtime/runtime.hpp"
#include "project_scripting_luau/playtest.hpp"
#include "project_scripting_luau/plugin.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"
#include "runtime_protocol/playtest.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace fei;

namespace {

struct ScriptFile {
    std::string_view path;
    Optional<std::string_view> content;
};

class TemporaryMixedScriptProject {
  public:
    explicit TemporaryMixedScriptProject(
        std::vector<ScriptFile> scripts,
        std::vector<ScriptFile> playtests = {},
        std::vector<ScriptFile> libraries = {}
    ) {
        static std::atomic<std::uint64_t> sequence {0};
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_root = std::filesystem::temp_directory_path() /
                 ("fei-project-mixed-scripts-" + std::to_string(timestamp) +
                  "-" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(m_root / "assets" / "scripts");

        const bool has_lua =
            std::ranges::any_of(scripts, [](const auto& script) {
                return std::filesystem::path {script.path}.extension() ==
                       ".lua";
            });
        const auto contains_luau = [](const auto& entries) {
            return std::ranges::any_of(entries, [](const auto& script) {
                return std::filesystem::path {script.path}.extension() ==
                       ".luau";
            });
        };
        const bool has_luau = contains_luau(scripts) ||
                              contains_luau(playtests) ||
                              contains_luau(libraries);

        std::ofstream project_stream(project_file());
        project_stream << "name: Mixed Script Runtime\n"
                          "asset_directory: assets\n"
                          "runtime:\n  plugins:\n";
        if (has_lua) {
            project_stream << "    - project_runtime::LuaScripts\n";
        }
        if (has_luau) {
            project_stream << "    - project_runtime::LuauScripts\n";
        }
        project_stream << "scripts:\n";
        for (const auto& script : scripts) {
            project_stream << "  - project://" << script.path << "\n";
        }
        if (!playtests.empty()) {
            project_stream << "playtests:\n";
            for (const auto& playtest : playtests) {
                project_stream << "  - project://" << playtest.path << "\n";
            }
        }
        auto write_files = [this](const auto& entries) {
            for (const auto& script : entries) {
                if (!script.content) {
                    continue;
                }
                const auto path =
                    m_root / "assets" / std::filesystem::path(script.path);
                std::filesystem::create_directories(path.parent_path());
                std::ofstream script_stream(path);
                script_stream << *script.content;
            }
        };
        write_files(scripts);
        write_files(playtests);
        write_files(libraries);
    }

    ~TemporaryMixedScriptProject() {
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }

    TemporaryMixedScriptProject(const TemporaryMixedScriptProject&) = delete;
    TemporaryMixedScriptProject&
    operator=(const TemporaryMixedScriptProject&) = delete;

    [[nodiscard]] std::filesystem::path project_file() const {
        return m_root / "project.yaml";
    }

  private:
    std::filesystem::path m_root;
};

App load_app(const TemporaryMixedScriptProject& directory) {
    auto project = Project::load(directory.project_file());
    REQUIRE(project);

    App app;
    configure_project_runtime(app, std::move(*project));
    app.finish();
    return app;
}

void apply_script_queues(App& app) {
    app.run_schedule(PreUpdate);
    app.run_schedule(PostUpdate);
}

} // namespace

TEST_CASE(
    "Project Lua and Luau scripts load from one script list",
    "[project-runtime][lua][luau][script]"
) {
    TemporaryMixedScriptProject directory({
        ScriptFile {
            .path = "scripts/legacy.lua",
            .content = std::string_view {"-- Lua project entry\n"},
        },
        ScriptFile {
            .path = "scripts/gameplay.luau",
            .content = std::string_view {R"(
                return module {
                    name = "project.gameplay",
                    systems = {},
                }
            )"},
        },
    });
    auto app = load_app(directory);

    auto& lua = app.resource<project_runtime::LuaScriptsState>();
    auto& luau = app.resource<project_runtime::LuauScriptsState>();
    REQUIRE(lua.scripts.size() == 1);
    REQUIRE(luau.scripts.size() == 1);
    CHECK(lua.scripts[0].reference.fallback_path.path().extension() == ".lua");
    CHECK(
        luau.scripts[0].reference.fallback_path.path().extension() == ".luau"
    );

    apply_script_queues(app);

    CHECK(lua.scripts[0].status == project_runtime::LuaScriptStatus::Loaded);
    CHECK(luau.scripts[0].status == project_runtime::LuauScriptStatus::Loaded);
    CHECK(lua.scripts[0].module.has_value());
    CHECK(luau.scripts[0].module.has_value());
}

TEST_CASE(
    "Project Luau scripts require cached library modules",
    "[project-runtime][luau][script][require]"
) {
    TemporaryMixedScriptProject directory(
        {
            ScriptFile {
                .path = "scripts/gameplay.luau",
                .content = std::string_view {R"(
                    local first = require("./lib/counter")
                    local second = require("./lib/../lib/counter.luau")

                    local function tick(state: ResRW<RequireState>)
                        state.value = first.next() * 10 + second.next()
                    end

                    return module {
                        name = "project.require_test",
                        types = {
                            RequireState = {
                                value = field(i32, 0),
                            },
                        },
                        resources = {
                            RequireState = {},
                        },
                        systems = {
                            [Update] = { tick },
                        },
                    }
                )"},
            },
        },
        {},
        {
            ScriptFile {
                .path = "scripts/lib/counter.luau",
                .content = std::string_view {R"(
                    local value = 0
                    return {
                        next = function()
                            value += 1
                            return value
                        end,
                    }
                )"},
            },
        }
    );
    auto app = load_app(directory);

    const auto& scripts = app.resource<project_runtime::LuauScriptsState>();
    REQUIRE(scripts.scripts.size() == 1);
    REQUIRE(
        scripts.scripts[0].status == project_runtime::LuauScriptStatus::Loaded
    );

    app.run_schedule(Update);
    auto state_type =
        Registry::instance().try_get_type("project.require_test.RequireState");
    REQUIRE(state_type);
    Ref state = app.world().resource(state_type->id());
    auto value = Registry::instance()
                     .get_cls(state_type->id())
                     .get_property("value")
                     .get(state);
    REQUIRE(value);
    CHECK(value->get<int>() == 12);
}

TEST_CASE(
    "Project Luau scripts reject circular library imports",
    "[project-runtime][luau][script][require][cycle]"
) {
    TemporaryMixedScriptProject directory(
        {
            ScriptFile {
                .path = "scripts/gameplay.luau",
                .content = std::string_view {R"(
                    local first = require("./lib/first")
                    return module {
                        name = "project.require_cycle",
                        systems = {},
                    }
                )"},
            },
        },
        {},
        {
            ScriptFile {
                .path = "scripts/lib/first.luau",
                .content = std::string_view {R"(
                    local second = require("./second")
                    return { second = second }
                )"},
            },
            ScriptFile {
                .path = "scripts/lib/second.luau",
                .content = std::string_view {R"(
                    local first = require("./first")
                    return { first = first }
                )"},
            },
        }
    );
    auto app = load_app(directory);

    const auto& scripts = app.resource<project_runtime::LuauScriptsState>();
    REQUIRE(scripts.scripts.size() == 1);
    CHECK(
        scripts.scripts[0].status == project_runtime::LuauScriptStatus::Failed
    );
    CHECK(scripts.scripts[0].error.contains("Circular Luau module dependency"));
    CHECK(scripts.scripts[0].error.contains("first.luau"));
    CHECK(scripts.scripts[0].error.contains("second.luau"));
}

TEST_CASE(
    "Project Luau scripts report invalid library imports",
    "[project-runtime][luau][script][require][error]"
) {
    SECTION("missing library") {
        TemporaryMixedScriptProject directory({
            ScriptFile {
                .path = "scripts/gameplay.luau",
                .content = std::string_view {R"(
                    local missing = require("./missing")
                    return module {
                        name = "project.missing_import",
                        systems = {},
                    }
                )"},
            },
        });
        auto app = load_app(directory);
        const auto& scripts = app.resource<project_runtime::LuauScriptsState>();
        REQUIRE(scripts.scripts.size() == 1);
        CHECK(
            scripts.scripts[0].status ==
            project_runtime::LuauScriptStatus::Failed
        );
        CHECK(scripts.scripts[0].error.contains("not found"));
    }

    SECTION("dynamic import") {
        TemporaryMixedScriptProject directory({
            ScriptFile {
                .path = "scripts/gameplay.luau",
                .content = std::string_view {R"(
                    local name = "missing"
                    local missing = require("./" .. name)
                    return module {
                        name = "project.dynamic_import",
                        systems = {},
                    }
                )"},
            },
        });
        auto app = load_app(directory);
        const auto& scripts = app.resource<project_runtime::LuauScriptsState>();
        REQUIRE(scripts.scripts.size() == 1);
        CHECK(
            scripts.scripts[0].status ==
            project_runtime::LuauScriptStatus::Failed
        );
        CHECK(scripts.scripts[0].error.contains("string literal"));
    }

    SECTION("path escape") {
        TemporaryMixedScriptProject directory({
            ScriptFile {
                .path = "scripts/gameplay.luau",
                .content = std::string_view {R"(
                    local outside = require("../../outside")
                    return module {
                        name = "project.escaped_import",
                        systems = {},
                    }
                )"},
            },
        });
        auto app = load_app(directory);
        const auto& scripts = app.resource<project_runtime::LuauScriptsState>();
        REQUIRE(scripts.scripts.size() == 1);
        CHECK(
            scripts.scripts[0].status ==
            project_runtime::LuauScriptStatus::Failed
        );
        CHECK(scripts.scripts[0].error.contains("escapes its asset source"));
    }

    SECTION("non-table export") {
        TemporaryMixedScriptProject directory(
            {
                ScriptFile {
                    .path = "scripts/gameplay.luau",
                    .content = std::string_view {R"(
                        local invalid = require("./invalid")
                        return module {
                            name = "project.invalid_export",
                            systems = {},
                        }
                    )"},
                },
            },
            {},
            {
                ScriptFile {
                    .path = "scripts/invalid.luau",
                    .content = std::string_view {"return 42"},
                },
            }
        );
        auto app = load_app(directory);
        const auto& scripts = app.resource<project_runtime::LuauScriptsState>();
        REQUIRE(scripts.scripts.size() == 1);
        CHECK(
            scripts.scripts[0].status ==
            project_runtime::LuauScriptStatus::Failed
        );
        CHECK(scripts.scripts[0].error.contains("must return a table"));
    }
}

TEST_CASE(
    "Project Luau playtests declare actions and observations",
    "[project-runtime][luau][playtest]"
) {
    TemporaryMixedScriptProject directory(
        {
            ScriptFile {
                .path = "scripts/gameplay.luau",
                .content = std::string_view {R"(
                    return module {
                        name = "project.playtest_game",
                        types = {
                            Control = {
                                value = field(i32, 0),
                            },
                        },
                        resources = {
                            Control = {},
                        },
                        systems = {},
                    }
                )"},
            },
        },
        {
            ScriptFile {
                .path = "scripts/main.playtest.luau",
                .content = std::string_view {R"(
                    local function begin_step(ctx, action)
                        ctx:resource(Control).value = action.value
                    end

                    local function end_step(ctx)
                        ctx:resource(Control).value = 0
                    end

                    local function observe(ctx)
                        return {
                            value = ctx:resource(Control).value,
                        }
                    end

                    return playtest {
                        id = "game.main",
                        label = "Main controls",
                        types = {
                            Control = "project.playtest_game.Control",
                        },
                        ticks = {
                            default = 4,
                            min = 1,
                            max = 10,
                            overridable = true,
                        },
                        action = {
                            type = "object",
                            properties = {
                                value = {type = "integer"},
                            },
                            required = {"value"},
                        },
                        observation = {
                            type = "object",
                            properties = {
                                value = {type = "integer"},
                            },
                            required = {"value"},
                        },
                        begin_step = begin_step,
                        end_step = end_step,
                        observe = observe,
                    }
                )"},
            },
        }
    );
    auto project = Project::load(directory.project_file());
    REQUIRE(project);

    App app;
    app.add_resource(runtime_protocol::PlaytestRegistry {});
    configure_project_runtime(app, std::move(*project));
    app.add_plugin(project_runtime::LuauPlaytestsPlugin {});
    app.finish();

    auto& registry = app.resource<runtime_protocol::PlaytestRegistry>();
    const auto* interface = registry.find("game.main");
    REQUIRE(interface != nullptr);
    CHECK(interface->descriptor.decision_ticks == 4);
    CHECK(interface->descriptor.minimum_ticks == 1);
    CHECK(interface->descriptor.maximum_ticks == 10);
    CHECK(
        nlohmann::json::parse(interface->descriptor.action_schema_json)
            .at("properties")
            .contains("value")
    );

    REQUIRE(interface->begin_step(app.world(), R"({"value":7})"));
    auto observation = interface->observe(app.world());
    REQUIRE(observation);
    CHECK(nlohmann::json::parse(*observation).at("value") == 7);
    REQUIRE(interface->end_step(app.world()));

    observation = interface->observe(app.world());
    REQUIRE(observation);
    CHECK(nlohmann::json::parse(*observation).at("value") == 0);
}

TEST_CASE(
    "Project Luau playtests require the playtest declaration helper",
    "[project-runtime][luau][playtest]"
) {
    TemporaryMixedScriptProject directory(
        {
            ScriptFile {
                .path = "scripts/gameplay.luau",
                .content = std::string_view {R"(
                    return module {
                        name = "project.invalid_playtest",
                        systems = {},
                    }
                )"},
            },
        },
        {
            ScriptFile {
                .path = "scripts/main.playtest.luau",
                .content = std::string_view {"return {}"},
            },
        }
    );
    auto project = Project::load(directory.project_file());
    REQUIRE(project);

    App app;
    app.add_resource(runtime_protocol::PlaytestRegistry {});
    configure_project_runtime(app, std::move(*project));
    app.add_plugin(project_runtime::LuauPlaytestsPlugin {});

    REQUIRE_THROWS_AS(app.finish(), std::runtime_error);
}

TEST_CASE(
    "Project Luau scripts preserve module compilation failures",
    "[project-runtime][luau][script]"
) {
    TemporaryMixedScriptProject directory({
        ScriptFile {
            .path = "scripts/broken.luau",
            .content = std::string_view {"local function ("},
        },
    });
    auto app = load_app(directory);

    apply_script_queues(app);

    const auto& state = app.resource<project_runtime::LuauScriptsState>();
    REQUIRE(state.scripts.size() == 1);
    CHECK(state.scripts[0].status == project_runtime::LuauScriptStatus::Failed);
    CHECK_FALSE(state.scripts[0].error.empty());
}

TEST_CASE(
    "Project Luau scripts preserve missing asset failures",
    "[project-runtime][luau][script]"
) {
    TemporaryMixedScriptProject directory({
        ScriptFile {
            .path = "scripts/missing.luau",
            .content = nullopt,
        },
    });
    auto app = load_app(directory);

    const auto& state = app.resource<project_runtime::LuauScriptsState>();
    REQUIRE(state.scripts.size() == 1);
    CHECK(state.scripts[0].status == project_runtime::LuauScriptStatus::Failed);
    CHECK(state.scripts[0].error.contains("not found"));
}

TEST_CASE(
    "Pure Luau project declares initializes and updates ECS state",
    "[project-runtime][luau][script][types][ecs]"
) {
    TemporaryMixedScriptProject directory({
        ScriptFile {
            .path = "scripts/gameplay.luau",
            .content = std::string_view {R"(
                local function initialize(
                    world: World,
                    state: ResRW<ProjectState>
                )
                    if state.mover ~= 0 then
                        return
                    end
                    local mover = world:spawn(
                        Position.new(),
                        Velocity.new()
                    )
                    state.mover = mover:id()
                end

                local function move(
                    movers: Query<Write<Position>, Read<Velocity>>,
                    state: ResRW<ProjectState>
                )
                    for position, velocity in movers do
                        position.x += velocity.x
                    end
                    state.ticks += 1
                end

                return module {
                    name = "project.pure_luau",
                    types = {
                        Position = {
                            x = field(f32, 1.0),
                        },
                        Velocity = {
                            x = field(f32, 2.0),
                        },
                        ProjectState = {
                            mover = field(entity, 0),
                            ticks = field(i32, 0),
                        },
                    },
                    resources = {
                        ProjectState = {},
                    },
                    systems = {
                        system(StartUp, initialize),
                        system(Update, move),
                    },
                }
            )"},
        },
    });
    auto app = load_app(directory);

    const auto& scripts = app.resource<project_runtime::LuauScriptsState>();
    REQUIRE(scripts.scripts.size() == 1);
    REQUIRE(
        scripts.scripts[0].status == project_runtime::LuauScriptStatus::Loaded
    );

    app.startup();
    app.run_schedule(Update);

    auto& registry = Registry::instance();
    auto position_type = registry.try_get_type("project.pure_luau.Position");
    auto state_type = registry.try_get_type("project.pure_luau.ProjectState");
    REQUIRE(position_type.has_value());
    REQUIRE(state_type.has_value());

    const Ref state = app.world().resource(state_type->id());
    auto& state_cls = registry.get_cls(state_type->id());
    auto mover = state_cls.get_property("mover").get(state);
    auto ticks = state_cls.get_property("ticks").get(state);
    REQUIRE(mover.has_value());
    REQUIRE(ticks.has_value());
    CHECK(ticks->get<int>() == 1);

    const Entity entity = mover->to_number<Entity>();
    REQUIRE(app.world().has_component(entity, position_type->id()));
    auto& position_cls = registry.get_cls(position_type->id());
    auto x = position_cls.get_property("x").get(
        app.world().get_component(entity, position_type->id())
    );
    REQUIRE(x.has_value());
    CHECK(x->get<float>() == 3.0F);
}
