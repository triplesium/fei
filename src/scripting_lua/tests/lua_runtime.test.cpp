#include "core/transform.hpp"
#include "lua_test_types.hpp"
#include "math/vector.hpp"
#include "refl/dynamic_array.hpp"
#include "refl/dynamic_map.hpp"
#include "refl/ref_utils.hpp"
#include "refl/registry.hpp"
#include "scripting_lua/runtime.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace fei;

TEST_CASE(
    "LuaRuntime invokes reflected indexed container methods",
    "[scripting][container]"
) {
    using TestVector = std::vector<int>;

    Registry& registry = Registry::instance();
    registry.register_type<TestVector>();

    auto runtime = make_test_runtime();
    runtime.bind_type(type<TestVector>());

    TestVector values {1};
    runtime.set_global("values", Ref(values));

    auto status = runtime.run_script(R"(
        assert(values:size() == 1)
        assert(values:at(0) == 1)
        assert(#values == 1)
        assert(values[0] == 1)

        values:append(2)
        values:insert(1, 3)
        values:assign(0, 4)

        assert(values:size() == 3)
        assert(values:at(0) == 4)
        assert(values:at(1) == 3)
        assert(values:at(2) == 2)
        assert(#values == 3)
        assert(values[0] == 4)
        assert(values[1] == 3)
        assert(values[2] == 2)

        values[1] = 8
        assert(values[1] == 8)

        local visited = 0
        for index, value in pairs(values) do
            assert(index == visited)
            assert(value == values[index])
            visited = visited + 1
        end
        assert(visited == #values)

        local out_of_range_ok = pcall(function()
            return values[99]
        end)
        assert(not out_of_range_ok)

        values:erase(2)
        assert(values:size() == 2)

        values:clear()
        assert(values:size() == 0)
        assert(#values == 0)
    )");

    REQUIRE(status);
    REQUIRE(values.empty());
}

TEST_CASE(
    "LuaRuntime invokes reflected associative container methods",
    "[scripting][container]"
) {
    using TestMap = std::map<int, std::string>;
    using TestSet = std::set<int>;

    Registry& registry = Registry::instance();
    registry.register_type<TestMap>();
    registry.register_type<TestSet>();

    auto runtime = make_test_runtime();
    runtime.bind_type(type<TestMap>());
    runtime.bind_type(type<TestSet>());

    TestMap scores;
    TestSet tags;
    runtime.set_global("scores", Ref(scores));
    runtime.set_global("tags", Ref(tags));

    auto status = runtime.run_script(R"(
        scores:insert(1, "one")
        scores:insert(2, "two")
        assert(scores:size() == 2)
        assert(#scores == 2)
        assert(scores:contains(1))
        assert(scores:find(2) == "two")

        scores:erase(1)
        assert(not scores:contains(1))

        tags:insert(3)
        tags:insert(5)
        assert(tags:size() == 2)
        assert(#tags == 2)
        assert(tags:contains(3))
        assert(tags:find(5) == 5)

        tags:erase(3)
        assert(not tags:contains(3))

        scores:clear()
        tags:clear()
    )");

    REQUIRE(status);
    REQUIRE(scores.empty());
    REQUIRE(tags.empty());
}

TEST_CASE(
    "LuaRuntime invokes reflected dynamic container methods",
    "[scripting][container][dynamic]"
) {
    Registry& registry = Registry::instance();
    registry.register_type<int>();
    registry.register_type<std::string>();

    auto array_result = DynamicArray::create(type_id<int>());
    auto map_result =
        DynamicMap::create(type_id<int>(), type_id<std::string>());
    REQUIRE(array_result);
    REQUIRE(map_result);

    auto runtime = make_test_runtime();
    runtime.bind_type(type<DynamicArray>());
    runtime.bind_type(type<DynamicMap>());

    auto& array = *array_result;
    auto& map = *map_result;
    runtime.set_global("array", Ref(array));
    runtime.set_global("map", Ref(map));

    auto status = runtime.run_script(R"(
        array:append(7)
        array:append(9)
        assert(array:size() == 2)
        assert(#array == 2)
        assert(array:at(0) == 7)
        assert(array:at(1) == 9)
        assert(array[0] == 7)
        assert(array[1] == 9)

        array[1] = 11
        assert(array[1] == 11)

        local total = 0
        for index, value in pairs(array) do
            assert(value == array[index])
            total = total + value
        end
        assert(total == 18)

        local array_ok = pcall(function()
            array:append("wrong")
        end)
        assert(not array_ok)

        map:insert(4, "four")
        assert(map:size() == 1)
        assert(#map == 1)
        assert(map:contains(4))
        assert(map:find(4) == "four")

        local map_ok = pcall(function()
            map:insert(4, 5)
        end)
        assert(not map_ok)
    )");

    REQUIRE(status);
    REQUIRE(array.size() == 2);
    REQUIRE(map.size() == 1);
    REQUIRE(array.at(0)->get_const<int>() == 7);
    REQUIRE(array.at(1)->get_const<int>() == 11);
    int map_key = 4;
    REQUIRE(map.find(Ref(map_key))->get_const<std::string>() == "four");
}

TEST_CASE(
    "LuaRuntime rejects mutation through const container refs",
    "[scripting][container][const]"
) {
    using TestVector = std::vector<int>;

    Registry& registry = Registry::instance();
    registry.register_type<TestVector>();

    auto runtime = make_test_runtime();
    runtime.bind_type(type<TestVector>());

    const TestVector values {1, 2};
    runtime.set_global("values", Ref(values));

    auto status = runtime.run_script(R"(
        assert(values:size() == 2)
        assert(values:at(0) == 1)
        assert(#values == 2)
        assert(values[0] == 1)

        local visited = 0
        for index, value in pairs(values) do
            assert(index == visited)
            assert(value == values[index])
            visited = visited + 1
        end
        assert(visited == #values)

        local ok = pcall(function()
            values:append(3)
        end)
        assert(not ok)

        local assign_ok = pcall(function()
            values[0] = 9
        end)
        assert(not assign_ok)
    )");

    REQUIRE(status);
    REQUIRE(values == TestVector {1, 2});
}

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
        LuaScriptSource {
            .name = "first.lua",
            .content = R"(
            local counter = 0
            function on_update(target)
                counter = counter + 1
                target.value = target.value + counter
            end
        )",
        }
    );
    auto second = runtime.load_module(
        LuaScriptSource {
            .name = "second.lua",
            .content = R"(
            local counter = 0
            function on_update(target)
                counter = counter + 1
                target.value = target.value + counter
            end
        )",
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
        LuaScriptSource {
            .name = "globals.lua",
            .content = R"(
            function on_update()
                receiver.value = receiver.value + 6
            end
        )",
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
        LuaScriptSource {
            .name = "reload.lua",
            .content = R"(
                function on_update(target)
                    target.value = target.value + 1
                end
            )",
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
        LuaScriptSource {
            .name = "reload.lua",
            .content = R"(
                function on_update(target)
                    target.value = target.value + 10
                end
            )",
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
    runtime.bind_type(type<Vector3>());
    runtime.bind_type(type<Quaternion>());
    runtime.bind_type(type<Transform3d>());

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
    runtime.bind_type(type<ScriptBareType>());

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
