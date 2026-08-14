#include "scripting/module_install.hpp"

#include "ecs/commands.hpp"
#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"
#include "refl/val.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

using namespace fei;

namespace {

ScriptModuleDecl
make_module_decl(std::string qualified_name, int default_value) {
    ScriptModuleDecl decl {
        .name = "test.module",
        .source_name = "test.script",
    };
    decl.types.push_back(
        ScriptTypeDecl {
            .name = "Config",
            .qualified_name = std::move(qualified_name),
            .fields = {
                ScriptFieldDecl {
                    .name = "value",
                    .type =
                        ScriptTypeRef {
                            .type_name = "int",
                            .type_id = type_id<int>(),
                        },
                    .default_value = make_val<int>(default_value),
                    .has_default = true,
                },
            },
        }
    );
    return decl;
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
    "Script module types reuse identical dynamic schemas",
    "[scripting][types]"
) {
    auto first = make_module_decl("tests.scripting.ReloadConfig", 7);
    auto first_bindings = ensure_script_module_types(first);
    REQUIRE(first_bindings);
    REQUIRE(first_bindings->size() == 1);

    auto second = make_module_decl("tests.scripting.ReloadConfig", 7);
    auto second_bindings = ensure_script_module_types(second);
    REQUIRE(second_bindings);
    REQUIRE(second_bindings->size() == 1);
    CHECK((*second_bindings)[0].type == (*first_bindings)[0].type);

    auto conflicting = make_module_decl("tests.scripting.ReloadConfig", 8);
    auto conflict = ensure_script_module_types(conflicting);
    REQUIRE_FALSE(conflict);
    CHECK(
        conflict.error().message.find("schema conflicts") != std::string::npos
    );
}

TEST_CASE(
    "Script modules install shared resources and systems",
    "[scripting][install]"
) {
    auto decl = make_module_decl("tests.scripting.InstalledConfig", 3);
    decl.resources.push_back(
        ScriptResourceDecl {
            .type = "tests.scripting.InstalledConfig",
            .initial_values = {
                ScriptResourceFieldDecl {
                    .name = "value",
                    .value = make_val<int>(11),
                },
            },
        }
    );
    decl.systems.push_back(
        DynamicSystemDecl {
            .name = "tick",
            .schedule = 42,
        }
    );

    int bound_types = 0;
    int executions = 0;
    World world;
    world.add_resource(CommandsQueue {});
    auto installed = install_script_module(
        world,
        decl,
        [&](const ScriptTypeBinding& binding) -> Status<ScriptError> {
            REQUIRE(binding.type != nullptr);
            ++bound_types;
            return {};
        },
        [&](const DynamicSystemDecl&)
            -> Result<std::unique_ptr<DynamicSystemExecutor>, ScriptError> {
            std::unique_ptr<DynamicSystemExecutor> executor =
                std::make_unique<CountingExecutor>(executions);
            return executor;
        }
    );

    REQUIRE(installed);
    REQUIRE(installed->size() == 1);
    CHECK(bound_types == 1);

    auto type =
        Registry::instance().try_get_type("tests.scripting.InstalledConfig");
    REQUIRE(type);
    REQUIRE(world.has_resource(type->id()));
    auto cls = Registry::instance().try_get_cls(type->id());
    REQUIRE(cls);
    auto value = cls->get_property("value").get(world.resource(type->id()));
    REQUIRE(value);
    CHECK(value->get<int>() == 11);

    world.run_schedule(42);
    CHECK(executions == 1);
    CHECK(remove_script_module_systems(world, *installed));
    world.run_schedule(42);
    CHECK(executions == 1);
}
