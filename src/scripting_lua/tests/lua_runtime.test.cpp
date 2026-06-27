#include "core/transform.hpp"
#include "ecs/world.hpp"
#include "math/vector.hpp"
#include "refl/ref_utils.hpp"
#include "refl/registry.hpp"
#include "scripting_lua/entity.hpp"
#include "scripting_lua/runtime.hpp"
#include "scripting_lua/tests/lua_test_types.hpp"
#include "scripting_lua/world.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace fei;

TEST_CASE(
    "LuaRuntime reads properties, writes properties, and calls methods",
    "[scripting]"
) {
    auto runtime = make_test_runtime();
    ScriptTestReceiver receiver;
    receiver.value = 4;

    runtime.set_global("receiver", make_ref(receiver));
    runtime.run_script(R"(
        receiver.value = receiver.value + 3
        receiver:set_value(receiver.value + 5)
    )");

    REQUIRE(receiver.value == 12);
    REQUIRE(receiver.method_calls == 1);
}

TEST_CASE("LuaRuntime constructs reflected objects from Lua", "[scripting]") {
    auto runtime = make_test_runtime();
    ScriptTestReceiver receiver;

    runtime.set_global("receiver", make_ref(receiver));
    runtime.run_script(R"(
        local created = ScriptTestReceiver.new(21)
        created.mode = ScriptTestEnum.Active
        created:copy_to(receiver)
    )");

    REQUIRE(receiver.value == 21);
    REQUIRE(receiver.mode == ScriptTestEnum::Active);
}

TEST_CASE("LuaRuntime calls Lua functions with reflected refs", "[scripting]") {
    auto runtime = make_test_runtime();
    ScriptTestReceiver receiver;
    receiver.value = 10;

    runtime.run_script(R"(
        function update_receiver(target)
            target.value = target.value + 8
            target:set_mode(ScriptTestEnum.Active)
        end
    )");

    REQUIRE(runtime.call_function(
        "update_receiver",
        std::vector<Ref> {make_ref(receiver)}
    ));
    REQUIRE(receiver.value == 18);
    REQUIRE(receiver.mode == ScriptTestEnum::Active);
    REQUIRE(receiver.method_calls == 1);
}

TEST_CASE("LuaRuntime isolates loaded module environments", "[scripting]") {
    auto runtime = make_test_runtime();
    ScriptTestReceiver receiver;

    auto first = runtime.load_module(
        ScriptSource {
            .name = "first.lua",
            .content = R"(
            local counter = 0
            function on_update(target)
                counter = counter + 1
                target.value = target.value + counter
            end
        )",
            .language = "lua",
        }
    );
    auto second = runtime.load_module(
        ScriptSource {
            .name = "second.lua",
            .content = R"(
            local counter = 0
            function on_update(target)
                counter = counter + 1
                target.value = target.value + counter
            end
        )",
            .language = "lua",
        }
    );

    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(runtime.call_module_function(
        *first,
        "on_update",
        std::vector<Ref> {make_ref(receiver)}
    ));
    REQUIRE(runtime.call_module_function(
        *first,
        "on_update",
        std::vector<Ref> {make_ref(receiver)}
    ));
    REQUIRE(runtime.call_module_function(
        *second,
        "on_update",
        std::vector<Ref> {make_ref(receiver)}
    ));

    REQUIRE(receiver.value == 4);
}

TEST_CASE("LuaRuntime sets module environment globals", "[scripting]") {
    auto runtime = make_test_runtime();
    ScriptTestReceiver receiver;

    auto module = runtime.load_module(
        ScriptSource {
            .name = "globals.lua",
            .content = R"(
            function on_update()
                receiver.value = receiver.value + 6
            end
        )",
            .language = "lua",
        }
    );

    REQUIRE(module);
    REQUIRE(runtime.set_module_global(*module, "receiver", make_ref(receiver)));
    REQUIRE(runtime.call_module_function(*module, "on_update", {}));
    REQUIRE(runtime.unset_module_global(*module, "receiver"));
    REQUIRE(receiver.value == 6);
}

TEST_CASE("LuaRuntime reloads module code after unload", "[scripting]") {
    auto runtime = make_test_runtime();
    ScriptTestReceiver receiver;

    auto first = runtime.load_module(
        ScriptSource {
            .name = "reload.lua",
            .content = R"(
                function on_update(target)
                    target.value = target.value + 1
                end
            )",
            .language = "lua",
        }
    );

    REQUIRE(first);
    REQUIRE(runtime.call_module_function(
        *first,
        "on_update",
        std::vector<Ref> {make_ref(receiver)}
    ));
    REQUIRE(runtime.unload_module(*first));

    auto second = runtime.load_module(
        ScriptSource {
            .name = "reload.lua",
            .content = R"(
                function on_update(target)
                    target.value = target.value + 10
                end
            )",
            .language = "lua",
        }
    );

    REQUIRE(second);
    REQUIRE(runtime.call_module_function(
        *second,
        "on_update",
        std::vector<Ref> {make_ref(receiver)}
    ));
    REQUIRE(receiver.value == 11);
}

TEST_CASE(
    "LuaRuntime passes enum constants to reflected methods",
    "[scripting][enum]"
) {
    auto runtime = make_test_runtime();
    ScriptTestReceiver receiver;

    runtime.set_global("receiver", make_ref(receiver));
    runtime.run_script("receiver:set_mode(ScriptTestEnum.Active)");

    REQUIRE(receiver.mode == ScriptTestEnum::Active);
    REQUIRE(receiver.method_calls == 1);
}

TEST_CASE(
    "LuaRuntime assigns enum constants to reflected properties",
    "[scripting][enum]"
) {
    auto runtime = make_test_runtime();
    ScriptTestReceiver receiver;

    runtime.set_global("receiver", make_ref(receiver));
    runtime.run_script("receiver.mode = ScriptTestEnum.Active");

    REQUIRE(receiver.mode == ScriptTestEnum::Active);
    REQUIRE(receiver.method_calls == 0);
}

TEST_CASE("LuaRuntime applies reflected property conversions", "[scripting]") {
    auto runtime = make_test_runtime();
    ScriptTestReceiver receiver;

    runtime.set_global("receiver", make_ref(receiver));
    runtime.run_script(R"(
        receiver.scale = 4
        receiver.precise = 2.5

        local int_ok = pcall(function()
            receiver.value = 1.5
        end)
        local view_ok = pcall(function()
            receiver.view = "hello"
        end)
        if not int_ok and not view_ok then
            receiver.method_calls = 2
        end
    )");

    REQUIRE(receiver.scale == 4.0f);
    REQUIRE(receiver.precise == 2.5);
    REQUIRE(receiver.value == 0);
    REQUIRE(receiver.view.empty());
    REQUIRE(receiver.method_calls == 2);
}

TEST_CASE(
    "LuaRuntime supports nested reflected transform updates",
    "[scripting]"
) {
    register_transform_script_metadata();

    LuaRuntime runtime;
    runtime.register_type(type<Vector3>());
    runtime.register_type(type<Quaternion>());
    runtime.register_type(type<Transform3d>());

    Transform3d transform;
    transform.position = {1.0f, 2.0f, 3.0f};
    transform.set_euler({0.0f, 90.0f, 0.0f});

    runtime.set_global("transform", make_ref(transform));
    runtime.run_script(R"(
        local position = transform.position
        transform.position = position + transform:forward() * 10.0 * 0.5
        transform:set_euler(Vector3.new(45.0, 90.0, 0.0))
    )");

    REQUIRE(transform.position.x == Catch::Approx(-4.0f));
    REQUIRE(transform.position.y == Catch::Approx(2.0f));
    REQUIRE(transform.position.z == Catch::Approx(3.0f));

    auto expected_rotation =
        Quaternion::from_euler_degrees({45.0f, 90.0f, 0.0f});
    REQUIRE(transform.rotation.x == Catch::Approx(expected_rotation.x));
    REQUIRE(transform.rotation.y == Catch::Approx(expected_rotation.y));
    REQUIRE(transform.rotation.z == Catch::Approx(expected_rotation.z));
    REQUIRE(transform.rotation.w == Catch::Approx(expected_rotation.w));
}

TEST_CASE(
    "LuaRuntime passes Lua type tables as reflected TypeId arguments",
    "[scripting]"
) {
    register_script_test_metadata();
    register_transform_script_metadata();
    register_world_script_metadata();

    LuaRuntime runtime;
    runtime.register_type(type<ScriptTestReceiver>());
    runtime.register_type(type<Vector3>());
    runtime.register_type(type<Quaternion>());
    runtime.register_type(type<Transform3d>());
    runtime.register_type(type<LuaWorld>());
    runtime.register_type(type<LuaEntity>());

    World world;
    auto& receiver = world.add_resource(ScriptTestReceiver {});
    receiver.value = 5;

    auto entity_id = world.entity();
    Transform3d transform;
    transform.position = {1.0f, 2.0f, 3.0f};
    world.add_component(entity_id, transform);

    LuaWorld lua_world(&world);
    LuaEntity lua_entity(&world, entity_id);
    runtime.set_global("world", make_ref(lua_world));
    runtime.set_global("entity", make_ref(lua_entity));

    runtime.run_script(R"(
        local receiver = world:resource(ScriptTestReceiver)
        receiver.value = receiver.value + 7

        local transform = entity:component(Transform3d)
        transform.position.x = transform.position.x + 3.0
    )");

    REQUIRE(receiver.value == 12);
    REQUIRE(
        world.get_component<Transform3d>(entity_id).position.x ==
        Catch::Approx(4.0f)
    );
}

TEST_CASE(
    "LuaRuntime applies reflected weak argument conversions",
    "[scripting]"
) {
    auto runtime = make_test_runtime();
    ScriptTestReceiver receiver;

    runtime.set_global("receiver", make_ref(receiver));
    runtime.run_script(R"(
        receiver:set_scaled(4)
        receiver:set_text_size("hello")
    )");

    REQUIRE(receiver.value == 5);
    REQUIRE(receiver.method_calls == 2);
}

TEST_CASE("LuaRuntime rejects ambiguous reflected overloads", "[scripting]") {
    auto runtime = make_test_runtime();
    ScriptTestReceiver receiver;

    runtime.set_global("receiver", make_ref(receiver));
    runtime.run_script(R"(
        receiver:choose_number(2.5)
        local ok = pcall(function()
            receiver:choose_number(4)
        end)
        if not ok then
            receiver.value = 77
        end
    )");

    REQUIRE(receiver.value == 77);
    REQUIRE(receiver.method_calls == 1);
}

TEST_CASE("LuaRuntime maps reflected Result returns", "[scripting]") {
    auto runtime = make_test_runtime();
    ScriptTestReceiver receiver;

    runtime.set_global("receiver", make_ref(receiver));
    runtime.run_script(R"(
        local value, err = receiver:result_value(true)
        if err == nil then
            receiver.value = value
        end

        local missing, failed = receiver:result_value(false)
        if missing == nil then
            receiver.method_calls = failed.code
        end
    )");

    REQUIRE(receiver.value == 42);
    REQUIRE(receiver.method_calls == 7);
}

TEST_CASE("LuaRuntime maps reflected Status returns", "[scripting]") {
    auto runtime = make_test_runtime();
    ScriptTestReceiver receiver;

    runtime.set_global("receiver", make_ref(receiver));
    runtime.run_script(R"(
        local ok, err = receiver:status_value(true)
        if ok == true and err == nil then
            receiver.value = 31
        end

        local failed_ok, failed_err = receiver:status_value(false)
        if failed_ok == nil then
            receiver.method_calls = failed_err.code
        end
    )");

    REQUIRE(receiver.value == 31);
    REQUIRE(receiver.method_calls == 9);
}

TEST_CASE(
    "LuaRuntime raises Lua errors for invalid reflected calls",
    "[scripting]"
) {
    auto runtime = make_test_runtime();
    ScriptTestReceiver receiver;

    runtime.set_global("receiver", make_ref(receiver));
    runtime.run_script(R"(
        local method = receiver.set_value
        local ok = pcall(function()
            method(10, 1)
        end)
        if not ok then
            receiver.value = 55
        end
    )");

    REQUIRE(receiver.value == 55);
    REQUIRE(receiver.method_calls == 0);
}

TEST_CASE("LuaRuntime reports missing class metadata to Lua", "[scripting]") {
    auto runtime = make_test_runtime();
    Registry::instance().register_type<ScriptBareType>();
    runtime.register_type(type<ScriptBareType>());

    ScriptTestReceiver receiver;
    runtime.set_global("receiver", make_ref(receiver));
    runtime.run_script(R"(
        local ok, err = pcall(function()
            ScriptBareType.new()
        end)
        if not ok and string.find(err, "Class not found") then
            receiver:set_value(64)
        end
    )");

    REQUIRE(receiver.value == 64);
    REQUIRE(receiver.method_calls == 1);
}
