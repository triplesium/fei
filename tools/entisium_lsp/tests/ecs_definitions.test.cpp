#include "Flags.hpp"
#include "Luau/BuiltinDefinitions.h"
#include "Luau/Common.h"
#include "Luau/ConfigResolver.h"
#include "Luau/ExperimentalFlags.h"
#include "Luau/FileResolver.h"
#include "Luau/Frontend.h"

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

class MemoryFileResolver final : public Luau::FileResolver {
  public:
    std::unordered_map<Luau::ModuleName, std::string> sources;

    std::optional<Luau::SourceCode>
    readSource(const Luau::ModuleName& name) override {
        const auto found = sources.find(name);
        if (found == sources.end()) {
            return std::nullopt;
        }
        return Luau::SourceCode {
            .source = found->second,
            .type = Luau::SourceCode::Module,
        };
    }
};

std::string runtime_definitions() {
    std::ifstream input(
        std::filesystem::path {ETS_PROJECT_ROOT} /
            "tools/luau_defgen/entisium-runtime.d.luau",
        std::ios::binary
    );
    REQUIRE(input);
    return {
        std::istreambuf_iterator<char> {input},
        std::istreambuf_iterator<char> {},
    };
}

std::vector<unsigned int> error_lines(std::string source) {
    for (auto* flag = Luau::FValue<bool>::list; flag != nullptr;
         flag = flag->next) {
        if (std::strncmp(flag->name, "Luau", 4) == 0 &&
            !Luau::isAnalysisFlagExperimental(flag->name)) {
            flag->value = true;
        }
    }
    applyRequiredFlags();

    MemoryFileResolver files;
    files.sources.emplace("ecs-test", std::move(source));
    Luau::NullConfigResolver configs;
    configs.defaultConfig.mode = Luau::Mode::Strict;
    Luau::Frontend frontend {
        Luau::SolverMode::New,
        &files,
        &configs,
    };
    Luau::registerBuiltinGlobals(frontend, frontend.globals);

    auto definitions = runtime_definitions();
    definitions += R"(
        declare extern type TestTransform with
            position: number
        end
        declare TestTransform: {
            __ets_type: TestTransform?,
        }
    )";
    const auto loaded = frontend.loadDefinitionFile(
        frontend.globals,
        frontend.globals.globalScope,
        definitions,
        "@entisium",
        false
    );
    REQUIRE(loaded.success);

    const auto result = frontend.check("ecs-test");
    std::vector<unsigned int> lines;
    lines.reserve(result.errors.size());
    for (const auto& error : result.errors) {
        lines.push_back(error.location.begin.line);
    }
    return lines;
}

} // namespace

TEST_CASE(
    "runtime definitions type ECS system parameters",
    "[lsp][definitions][ecs]"
) {
    const auto errors = error_lines(R"(
        type TestEvent = { value: number }

        local function update(
            query: Query<Entity, Write<TestTransform>, With<TestTransform>>,
            writer: EventWriter<TestEvent>,
            state: State<"idle" | "running">,
            next_state: NextState<"idle" | "running">,
            removed: RemovedComponents<TestTransform>,
            commands: Commands
        )
            for entity, transform in query do
                local entity_id: number = entity
                local position: number = transform.position
                writer({ value = position })
                writer:send({ value = entity_id })
            end

            for removed_entity in removed do
                commands:entity(removed_entity):despawn()
            end

            if state:get() == "idle" then
                next_state:set("running")
            end
        end

        local configured = system(update):run_if(in_state("idle"))
        local chained = chain(configured, update)
        local function inspect_world(world: World)
            local transform = world:resource(TestTransform)
            if transform then
                local position: number = transform.position
            end
        end
        local plugin = Plugin.new {
            build = function(app: App)
                app:add_systems(MainSchedules.Update, configured, chained)
                app:add_system(OnEnter("idle"), update)
            end,
        }
    )");

    CHECK(errors.empty());
}

TEST_CASE(
    "runtime definitions preserve query item types",
    "[lsp][definitions][ecs]"
) {
    const auto errors = error_lines(R"(
        type TestEvent = { value: number }

        local function invalid(query: Query<Entity, Write<TestTransform>>)
            local _, first = query:first()
            local invalid_first: string = first.position
            for _, transform in query do
                local invalid_position: string = transform.position
            end
        end

        local direct = nil :: Write<TestTransform>?
        if direct then
            local invalid_direct: string = direct.position
        end

        local function invalid_world(world: World)
            local transform = world:resource(TestTransform)
            if transform then
                local invalid_resource: string = transform.position
            end
        end

        local function invalid_params(
            writer: EventWriter<TestEvent>,
            next_state: NextState<"idle" | "running">
        )
            writer({ value = "invalid" })
            next_state:set("paused")
        end

    )");

    CHECK(errors == std::vector<unsigned int> {5, 7, 13, 19, 27, 28});
}
