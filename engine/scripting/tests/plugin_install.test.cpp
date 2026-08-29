#include "scripting/detail/plugin_install.hpp"

#include "ecs/commands.hpp"
#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"
#include "refl/val.hpp"
#include "scripting/detail/borrow_scope.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

using namespace ets;

namespace {

LuauModuleSchema
make_module_schema(std::string qualified_name, int default_value) {
    LuauModuleSchema schema {
        .name = "test.module",
        .source_name = "test.script",
    };
    schema.types.push_back(
        LuauTypeDecl {
            .name = "Config",
            .qualified_name = std::move(qualified_name),
            .fields = {
                LuauFieldDecl {
                    .name = "value",
                    .type =
                        LuauTypeRef {
                            .type_name = "int",
                            .type_id = type_id<int>(),
                        },
                    .default_value = make_val<int>(default_value),
                    .has_default = true,
                },
            },
        }
    );
    return schema;
}

class CountingExecutor final : public DynamicSystemExecutor {
  private:
    int* m_count {nullptr};

  public:
    explicit CountingExecutor(int& count) : m_count(&count) {}

    Status<DynamicSystemError>
    execute(const std::vector<Ref>& /*args*/) override {
        ++*m_count;
        return {};
    }
};

} // namespace

TEST_CASE(
    "Luau borrow scopes expire and cannot end newer borrows",
    "[scripting][luau][borrow]"
) {
    LuauBorrowScope scope;
    const auto first = scope.begin();
    REQUIRE(scope.valid(first));

    const auto second = scope.begin();
    CHECK_FALSE(scope.valid(first));
    REQUIRE(scope.valid(second));

    scope.end(first);
    REQUIRE(scope.valid(second));
    scope.end(second);
    CHECK_FALSE(scope.valid(second));
}

TEST_CASE(
    "Luau schemas reuse identical dynamic types",
    "[scripting][luau][types]"
) {
    auto first = make_module_schema("tests.scripting.ReloadConfig", 7);
    auto first_bindings = ensure_luau_types(first);
    REQUIRE(first_bindings);
    REQUIRE(first_bindings->size() == 1);

    auto second = make_module_schema("tests.scripting.ReloadConfig", 7);
    auto second_bindings = ensure_luau_types(second);
    REQUIRE(second_bindings);
    REQUIRE(second_bindings->size() == 1);
    CHECK((*second_bindings)[0].type == (*first_bindings)[0].type);

    auto conflicting = make_module_schema("tests.scripting.ReloadConfig", 8);
    auto conflict = ensure_luau_types(conflicting);
    REQUIRE_FALSE(conflict);
    CHECK(
        conflict.error().message.find("schema conflicts") != std::string::npos
    );
}

TEST_CASE(
    "Luau runtime system groups install compiled systems",
    "[scripting][luau][install]"
) {
    auto schema = make_module_schema("tests.scripting.InstalledConfig", 3);
    std::vector<DynamicSystemDecl> systems;
    systems.push_back(
        DynamicSystemDecl {
            .name = "tick",
            .schedule = 42,
        }
    );

    int executions = 0;
    World world;
    world.add_resource(CommandsQueue {});
    auto bindings = ensure_luau_types(schema);
    REQUIRE(bindings);
    auto installed = install_luau_systems(
        world,
        "test.script",
        systems,
        [&](const DynamicSystemDecl&)
            -> Result<std::unique_ptr<DynamicSystemExecutor>, LuauScriptError> {
            std::unique_ptr<DynamicSystemExecutor> executor =
                std::make_unique<CountingExecutor>(executions);
            return executor;
        }
    );

    REQUIRE(installed);
    REQUIRE(installed->size() == 1);

    world.run_schedule(42);
    CHECK(executions == 1);
    CHECK(remove_luau_plugin_systems(world, *installed));
    world.run_schedule(42);
    CHECK(executions == 1);
}
