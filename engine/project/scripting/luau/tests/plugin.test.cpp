#include "core/plugin.hpp"

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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace ets;

namespace {

struct ScriptFile {
    std::string_view path;
    Optional<std::string_view> content;
};

class TemporaryMixedScriptProject {
  public:
    explicit TemporaryMixedScriptProject(
        std::vector<ScriptFile> scripts,
        std::vector<ScriptFile> libraries = {},
        Optional<std::string_view> game_plugin = nullopt
    ) {
        static std::atomic<std::uint64_t> sequence {0};
        const auto timestamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        m_root =
            std::filesystem::temp_directory_path() /
            ("entisium-project-mixed-scripts-" + std::to_string(timestamp) +
             "-" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(m_root / "assets" / "scripts");

        std::ofstream project_stream(project_file());
        project_stream << "name: Mixed Script Runtime\n"
                          "asset_directory: assets\n";
        if (game_plugin) {
            project_stream << "plugin: \"" << *game_plugin << "\"\n";
        } else if (!scripts.empty()) {
            project_stream << "plugin: \"project://" << scripts.front().path
                           << "#EntryPlugin\"\n";
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
    app.add_plugin<CorePlugin>();
    app.add_plugin<project_runtime::LuauScriptsPlugin>();
    app.finish();
    return app;
}

void apply_script_queues(App& app) {
    app.run_schedule(PreUpdate);
    app.run_schedule(PostUpdate);
}

} // namespace

TEST_CASE(
    "Project Luau scripts have no implicit entry Plugin",
    "[project-runtime][luau][plugin][entry]"
) {
    TemporaryMixedScriptProject directory(
        {},
        {
            ScriptFile {
                .path = "scripts/dormant.luau",
                .content = std::string_view {R"(
                    export local DormantPlugin = Plugin.new {
                        build = function(app: App)
                            app:add_system(Update, function()
                                error("dormant Plugin must not run")
                            end)
                        end,
                    }
                )"},
            },
        }
    );
    auto app = load_app(directory);

    CHECK(app.resource<project_runtime::LuauScriptsState>().scripts.empty());
    CHECK(app.resource<LuauScriptSystemRegistry>().size() == 0);
}

TEST_CASE(
    "Project Luau scripts require native engine modules",
    "[project-runtime][luau][script][require][native]"
) {
    TemporaryMixedScriptProject directory({
        ScriptFile {
            .path = "scripts/native_module.luau",
            .content = std::string_view {R"(
                local core = require("@entisium/core")

                local function verify(
                    transforms: Query<Read<core.Transform2d>>
                )
                    assert(core.Transform2d ~= nil)
                    assert(core.Time ~= nil)
                    assert(core.Random ~= nil)
                    assert(Time ~= nil)
                    assert(Random == nil)
                    assert(transforms ~= nil)
                end

                export local EntryPlugin = Plugin.new {
                    build = function(app: App)
                        app:add_system(Update, verify)
                    end,
                }
            )"},
        },
    });
    auto app = load_app(directory);

    apply_script_queues(app);

    const auto& scripts = app.resource<project_runtime::LuauScriptsState>();
    REQUIRE(scripts.scripts.size() == 1);
    REQUIRE(
        scripts.scripts[0].status == project_runtime::LuauScriptStatus::Loaded
    );
    app.run_schedule(Update);
}

TEST_CASE(
    "Project Luau scripts require cached dependency modules",
    "[project-runtime][luau][script][require]"
) {
    TemporaryMixedScriptProject directory(
        {
            ScriptFile {
                .path = "scripts/require_test.luau",
                .content = std::string_view {R"(
                    local first = require("./lib/counter")
                    local second = require("./lib/../lib/counter.luau")

                    local function tick(state: ResRW<RequireState>)
                        state.value = first.next(0) * 10 + second.next(1)
                    end

                    export type RequireState = {
                        value: i32,
                    }

                    export local EntryPlugin = Plugin.new {
                        build = function(app: App)
                            app:add_resource(RequireState { value = 0 })
                            app:add_system(Update, tick)
                        end,
                    }
                )"},
            },
        },
        {
            ScriptFile {
                .path = "scripts/lib/counter.luau",
                .content = std::string_view {R"(
                    export function next(value: number)
                        return value + 1
                    end

                    export local DormantPlugin = Plugin.new {
                        build = function(_app: App)
                            error("dependency Plugin must not be activated")
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
    auto state_type = Registry::instance().try_get_type(
        "project.scripts.require_test.RequireState"
    );
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
    "Project Luau scripts prepare imported dependency modules",
    "[project-runtime][luau][script][require][types]"
) {
    TemporaryMixedScriptProject directory(
        {
            ScriptFile {
                .path = "scripts/game.luau",
                .content = std::string_view {R"(
                    local Shared = require("./lib/shared")

                    export local EntryPlugin = Plugin.new {
                        build = function(app: App)
                            app:add_resource(Shared.SharedState { value = 1 })
                            app:add_system(Update, Shared.tick)
                        end,
                    }
                )"},
            },
        },
        {
            ScriptFile {
                .path = "scripts/lib/shared.luau",
                .content = std::string_view {R"(
                    export type SharedState = {
                        value: i32,
                    }

                    export function tick(state: ResRW<SharedState>)
                        assert(Time ~= nil)
                        state.value += 1
                    end
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

    auto state_type = Registry::instance().try_get_type(
        "project.scripts.lib.shared.SharedState"
    );
    REQUIRE(state_type);
    Ref state = app.world().resource(state_type->id());
    auto value = Registry::instance()
                     .get_cls(state_type->id())
                     .get_property("value")
                     .get(state);
    REQUIRE(value);
    CHECK(value->get<int>() == 2);
}

TEST_CASE(
    "Project Luau scripts reject circular module imports",
    "[project-runtime][luau][script][require][cycle]"
) {
    TemporaryMixedScriptProject directory(
        {
            ScriptFile {
                .path = "scripts/gameplay.luau",
                .content = std::string_view {R"(
                    local first = require("./lib/first")
                    export local EntryPlugin = Plugin.new {
                        build = function(app: App)
                        end,
                    }
                )"},
            },
        },
        {
            ScriptFile {
                .path = "scripts/lib/first.luau",
                .content = std::string_view {R"(
                    local second = require("./second")
                    export local dependency = second
                )"},
            },
            ScriptFile {
                .path = "scripts/lib/second.luau",
                .content = std::string_view {R"(
                    local first = require("./first")
                    export local dependency = first
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
    "Project Luau scripts report invalid module imports",
    "[project-runtime][luau][script][require][error]"
) {
    SECTION("missing module") {
        TemporaryMixedScriptProject directory({
            ScriptFile {
                .path = "scripts/gameplay.luau",
                .content = std::string_view {R"(
                    local missing = require("./missing")
                    export local EntryPlugin = Plugin.new {
                        dependencies = { missing.DependencyPlugin },
                        build = function(app: App)
                        end,
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
                    export local EntryPlugin = Plugin.new {
                        dependencies = { missing.DependencyPlugin },
                        build = function(app: App)
                        end,
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
                    export local EntryPlugin = Plugin.new {
                        dependencies = { outside.DependencyPlugin },
                        build = function(app: App)
                        end,
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

    SECTION("top-level return") {
        TemporaryMixedScriptProject directory(
            {
                ScriptFile {
                    .path = "scripts/gameplay.luau",
                    .content = std::string_view {R"(
                        local invalid = require("./invalid")
                        export local EntryPlugin = Plugin.new {
                            dependencies = { invalid.DependencyPlugin },
                            build = function(app: App)
                            end,
                        }
                    )"},
                },
            },
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
        CHECK(
            scripts.scripts[0].error.contains("top-level return declarations")
        );
    }
}

TEST_CASE(
    "Project Luau Plugins register playtest actions and observations",
    "[project-runtime][luau][playtest][plugin]"
) {
    TemporaryMixedScriptProject directory(
        {},
        {
            ScriptFile {
                .path = "scripts/game.luau",
                .content = std::string_view {R"(
                    export type Control = {
                        value: i32,
                    }

                    local function begin_step(ctx, action)
                        ctx:resource(Control).value = action.value
                    end

                    local function end_step(ctx)
                        ctx:resource(Control).value = 0
                    end

                    local function observe(ctx)
                        return { value = ctx:resource(Control).value }
                    end

                    export local GamePlugin = Plugin.new {
                        build = function(app: App)
                            app:insert_resource(Control { value = 0 })
                            app:add_playtest {
                                id = "game.main",
                                label = "Main controls",
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
                        end,
                    }
                )"},
            },
        },
        std::string_view {"project://scripts/game.luau#GamePlugin"}
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
    "Project Luau Plugin dependencies own playtest callbacks",
    "[project-runtime][luau][playtest][plugin][dependency]"
) {
    TemporaryMixedScriptProject directory(
        {},
        {
            ScriptFile {
                .path = "scripts/playtest.luau",
                .content = std::string_view {R"(
                    export type Control = {
                        value: i32,
                    }

                    local function begin_step(ctx, action)
                        ctx:resource(Control).value = action.value
                    end

                    local function end_step(ctx)
                        ctx:resource(Control).value = 0
                    end

                    local function observe(ctx)
                        return { value = ctx:resource(Control).value }
                    end

                    export local PlaytestPlugin = Plugin.new {
                        build = function(app: App)
                            app:insert_resource(Control { value = 0 })
                            app:add_playtest {
                                id = "game.dependency",
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
                        end,
                    }
                )"},
            },
            ScriptFile {
                .path = "scripts/game.luau",
                .content = std::string_view {R"(
                    local Playtest = require("./playtest")

                    export local GamePlugin = Plugin.new {
                        dependencies = {
                            Playtest.PlaytestPlugin,
                        },
                        build = function(_app: App)
                        end,
                    }
                )"},
            },
        },
        std::string_view {"project://scripts/game.luau#GamePlugin"}
    );
    auto project = Project::load(directory.project_file());
    REQUIRE(project);

    App app;
    app.add_resource(runtime_protocol::PlaytestRegistry {});
    configure_project_runtime(app, std::move(*project));
    app.add_plugin(project_runtime::LuauPlaytestsPlugin {});
    app.finish();

    auto& registry = app.resource<runtime_protocol::PlaytestRegistry>();
    const auto* interface = registry.find("game.dependency");
    REQUIRE(interface != nullptr);
    REQUIRE(interface->begin_step(app.world(), R"({"value":9})"));
    auto observation = interface->observe(app.world());
    REQUIRE(observation);
    CHECK(nlohmann::json::parse(*observation).at("value") == 9);
    REQUIRE(interface->end_step(app.world()));
    observation = interface->observe(app.world());
    REQUIRE(observation);
    CHECK(nlohmann::json::parse(*observation).at("value") == 0);
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
    "Project installs a named exported Luau game Plugin",
    "[project-runtime][luau][plugin][export]"
) {
    TemporaryMixedScriptProject directory(
        {},
        {
            ScriptFile {
                .path = "scripts/game.luau",
                .content = std::string_view {R"(
                    export type Counter = {
                        value: i32,
                    }

                    local function tick(counter: ResRW<Counter>)
                        counter.value += 3
                    end

                    local function unused(counter: ResRW<Counter>)
                        counter.value += 100
                    end

                    export local GamePlugin = Plugin.new {
                        build = function(app: App)
                            app:insert_resource(Counter { value = 2 })
                            app:add_system(Update, tick)
                        end,
                    }

                    export local UnusedPlugin = Plugin.new {
                        build = function(app: App)
                            app:add_system(Update, unused)
                        end,
                    }
                )"},
            },
        },
        std::string_view {"project://scripts/game.luau#GamePlugin"}
    );
    auto app = load_app(directory);

    const auto& scripts = app.resource<project_runtime::LuauScriptsState>();
    REQUIRE(scripts.scripts.size() == 1);
    REQUIRE(
        scripts.scripts[0].status == project_runtime::LuauScriptStatus::Loaded
    );

    app.run_schedule(Update);

    auto counter_type =
        Registry::instance().try_get_type("project.scripts.game.Counter");
    REQUIRE(counter_type);
    const Ref counter = app.world().resource(counter_type->id());
    auto value = Registry::instance()
                     .get_cls(counter_type->id())
                     .get_property("value")
                     .get(counter);
    REQUIRE(value);
    CHECK(value->get<int>() == 5);
}

TEST_CASE(
    "Exported Luau Plugins install transitive dependencies",
    "[project-runtime][luau][plugin][export][dependency]"
) {
    TemporaryMixedScriptProject directory(
        {},
        {
            ScriptFile {
                .path = "scripts/player.luau",
                .content = std::string_view {R"(
                    export type Counter = {
                        value: i32,
                    }

                    local function player_tick(counter: ResRW<Counter>)
                        counter.value += 1
                    end

                    local function bonus_tick(counter: ResRW<Counter>)
                        counter.value += 100
                    end

                    export local PlayerPlugin = Plugin.new {
                        build = function(app: App)
                            app:insert_resource(Counter { value = 1 })
                            app:add_system(Update, player_tick)
                        end,
                    }

                    export local BonusPlugin = Plugin.new {
                        build = function(app: App)
                            app:add_system(Update, bonus_tick)
                        end,
                    }
                )"},
            },
            ScriptFile {
                .path = "scripts/game.luau",
                .content = std::string_view {R"(
                    local Player = require("./player")

                    local function game_tick(counter: ResRW<Player.Counter>)
                        counter.value += 10
                    end

                    export local GamePlugin = Plugin.new {
                        dependencies = {
                            Player.PlayerPlugin,
                            Player.BonusPlugin,
                        },
                        build = function(app: App)
                            app:add_system(Update, game_tick)
                        end,
                    }
                )"},
            },
        },
        std::string_view {"project://scripts/game.luau#GamePlugin"}
    );
    auto app = load_app(directory);

    const auto& scripts = app.resource<project_runtime::LuauScriptsState>();
    REQUIRE(scripts.scripts.size() == 1);
    REQUIRE(
        scripts.scripts[0].status == project_runtime::LuauScriptStatus::Loaded
    );
    CHECK(app.has_plugin(
        PluginId {
            "project://scripts/game.luau#GamePlugin",
        }
    ));
    CHECK(app.has_plugin(
        PluginId {
            "project://scripts/player.luau#PlayerPlugin",
        }
    ));
    CHECK(app.has_plugin(
        PluginId {
            "project://scripts/player.luau#BonusPlugin",
        }
    ));

    app.run_schedule(Update);

    auto counter_type =
        Registry::instance().try_get_type("project.scripts.player.Counter");
    REQUIRE(counter_type);
    const Ref counter = app.world().resource(counter_type->id());
    auto value = Registry::instance()
                     .get_cls(counter_type->id())
                     .get_property("value")
                     .get(counter);
    REQUIRE(value);
    CHECK(value->get<int>() == 112);
}

TEST_CASE(
    "Exported Luau Plugins reject dependency cycles",
    "[project-runtime][luau][plugin][export][dependency]"
) {
    TemporaryMixedScriptProject directory(
        {},
        {
            ScriptFile {
                .path = "scripts/game.luau",
                .content = std::string_view {R"(
                    local Other = require("./other")

                    export local GamePlugin = Plugin.new {
                        dependencies = { Other.OtherPlugin },
                        build = function(app: App)
                        end,
                    }
                )"},
            },
            ScriptFile {
                .path = "scripts/other.luau",
                .content = std::string_view {R"(
                    local Game = require("./game")

                    export local OtherPlugin = Plugin.new {
                        dependencies = { Game.GamePlugin },
                        build = function(app: App)
                        end,
                    }
                )"},
            },
        },
        std::string_view {"project://scripts/game.luau#GamePlugin"}
    );
    auto app = load_app(directory);
    apply_script_queues(app);

    const auto& scripts = app.resource<project_runtime::LuauScriptsState>();
    REQUIRE(scripts.scripts.size() == 1);
    CHECK(
        scripts.scripts[0].status == project_runtime::LuauScriptStatus::Failed
    );
    CHECK(scripts.scripts[0].error.contains("Circular Luau Plugin dependency"));
}

TEST_CASE(
    "Pure Luau project declares initializes and updates ECS state",
    "[project-runtime][luau][script][types][ecs]"
) {
    TemporaryMixedScriptProject directory({
        ScriptFile {
            .path = "scripts/pure_luau.luau",
            .content = std::string_view {R"(
                export type Position = {
                    x: f32,
                }

                export type Velocity = {
                    x: f32,
                }

                export type ProjectState = {
                    mover: entity,
                    ticks: i32,
                }

                local function initialize(
                    world: World,
                    state: ResRW<ProjectState>
                )
                    if state.mover ~= 0 then
                        return
                    end
                    local mover = world:spawn(
                        Position.new { x = 1.0 },
                        Velocity.new { x = 2.0 }
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

                export local EntryPlugin = Plugin.new {
                    build = function(app: App)
                        app:add_resource(ProjectState {})
                        app:add_system(StartUp, initialize)
                        app:add_system(Update, move)
                    end,
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
    auto position_type =
        registry.try_get_type("project.scripts.pure_luau.Position");
    auto state_type =
        registry.try_get_type("project.scripts.pure_luau.ProjectState");
    REQUIRE(position_type.has_value());
    REQUIRE(state_type.has_value());

    const Ref state = app.world().resource(state_type->id());
    auto& state_cls = registry.get_cls(state_type->id());
    auto mover = state_cls.get_property("mover").get(state);
    auto ticks = state_cls.get_property("ticks").get(state);
    REQUIRE(mover.has_value());
    REQUIRE(ticks.has_value());
    CHECK(ticks->get<int>() == 1);

    const Entity entity = mover->get<Entity>();
    REQUIRE(app.world().has_component(entity, position_type->id()));
    auto& position_cls = registry.get_cls(position_type->id());
    auto x = position_cls.get_property("x").get(
        app.world().get_component(entity, position_type->id())
    );
    REQUIRE(x.has_value());
    CHECK(x->get<float>() == 3.0F);
}
