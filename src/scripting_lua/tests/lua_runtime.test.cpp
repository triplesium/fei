#include "scripting_lua/lua_runtime.hpp"

#include "asset/assets.hpp"
#include "asset/event.hpp"
#include "base/result.hpp"
#include "core/transform.hpp"
#include "ecs/event.hpp"
#include "ecs/world.hpp"
#include "math/vector.hpp"
#include "refl/cls.hpp"
#include "refl/enum.hpp"
#include "refl/ref_utils.hpp"
#include "refl/registry.hpp"
#include "scripting/asset.hpp"
#include "scripting/component.hpp"
#include "scripting_lua/entity.hpp"
#include "scripting_lua/systems.hpp"
#include "scripting_lua/world.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string_view>
#include <vector>

using namespace fei;

namespace {

enum class ScriptTestEnum { Idle = 1, Active = 2 };

struct ScriptTestError {
    int code {0};
};

struct ScriptBareType {
    int value {0};
};

struct ScriptTestReceiver {
    ScriptTestEnum mode {ScriptTestEnum::Idle};
    int value {0};
    int method_calls {0};
    float scale {0.0f};
    double precise {0.0};
    std::string_view view;

    ScriptTestReceiver() = default;
    explicit ScriptTestReceiver(int value) : value(value) {}

    void set_value(int next) {
        value = next;
        ++method_calls;
    }

    void set_mode(ScriptTestEnum next) {
        mode = next;
        ++method_calls;
    }

    void copy_to(ScriptTestReceiver& target) const {
        target.mode = mode;
        target.value = value;
    }

    void set_scaled(float factor) {
        value = static_cast<int>(factor * 2.0f);
        ++method_calls;
    }

    void set_text_size(std::string_view text) {
        value = static_cast<int>(text.size());
        ++method_calls;
    }

    void choose_number(float) {
        value = 1;
        ++method_calls;
    }

    void choose_number(double) {
        value = 2;
        ++method_calls;
    }

    Result<int, ScriptTestError> result_value(bool succeed) const {
        if (succeed) {
            return 42;
        }
        return failure(ScriptTestError {.code = 7});
    }

    Status<ScriptTestError> status_value(bool succeed) const {
        if (succeed) {
            return {};
        }
        return failure(ScriptTestError {.code = 9});
    }
};

void register_script_test_metadata() {
    auto& registry = Registry::instance();
    registry.register_enum<ScriptTestEnum>()
        .add_enumerator("Idle", static_cast<std::int64_t>(ScriptTestEnum::Idle))
        .add_enumerator(
            "Active",
            static_cast<std::int64_t>(ScriptTestEnum::Active)
        );

    registry.register_cls<ScriptTestError>().add_property(
        "code",
        &ScriptTestError::code
    );

    registry.register_cls<ScriptTestReceiver>()
        .add_property("mode", &ScriptTestReceiver::mode)
        .add_property("value", &ScriptTestReceiver::value)
        .add_property("method_calls", &ScriptTestReceiver::method_calls)
        .add_property("scale", &ScriptTestReceiver::scale)
        .add_property("precise", &ScriptTestReceiver::precise)
        .add_property("view", &ScriptTestReceiver::view)
        .add_method("set_value", &ScriptTestReceiver::set_value)
        .add_method("set_mode", &ScriptTestReceiver::set_mode)
        .add_method("copy_to", &ScriptTestReceiver::copy_to)
        .add_method("set_scaled", &ScriptTestReceiver::set_scaled)
        .add_method("set_text_size", &ScriptTestReceiver::set_text_size)
        .add_method(
            "choose_number",
            static_cast<void (ScriptTestReceiver::*)(float)>(
                &ScriptTestReceiver::choose_number
            )
        )
        .add_method(
            "choose_number",
            static_cast<void (ScriptTestReceiver::*)(double)>(
                &ScriptTestReceiver::choose_number
            )
        )
        .add_method("result_value", &ScriptTestReceiver::result_value)
        .add_method("status_value", &ScriptTestReceiver::status_value)
        .add_constructor<ScriptTestReceiver, int>();
}

void register_transform_script_metadata() {
    auto& registry = Registry::instance();
    registry.register_cls<Vector3>()
        .add_property("x", &Vector3::x)
        .add_property("y", &Vector3::y)
        .add_property("z", &Vector3::z)
        .add_method(
            "operator+",
            static_cast<Vector3 (Vector3::*)(const Vector3&) const>(
                &Vector3::operator+
            )
        )
        .add_method(
            "operator-",
            static_cast<Vector3 (Vector3::*)(const Vector3&) const>(
                &Vector3::operator-
            )
        )
        .add_method(
            "operator*",
            static_cast<Vector3 (Vector3::*)(float) const>(&Vector3::operator*)
        )
        .add_method(
            "operator/",
            static_cast<Vector3 (Vector3::*)(float) const>(&Vector3::operator/)
        )
        .add_constructor<Vector3>()
        .add_constructor<Vector3, float, float, float>();

    registry.register_cls<Transform3d>()
        .add_property("position", &Transform3d::position)
        .add_property("rotation", &Transform3d::rotation)
        .add_property("scale", &Transform3d::scale)
        .add_method("forward", &Transform3d::forward)
        .add_method("right", &Transform3d::right)
        .add_method("up", &Transform3d::up);
}

void register_world_script_metadata() {
    auto& registry = Registry::instance();
    registry.register_cls<LuaWorld>()
        .add_method("resource", &LuaWorld::resource)
        .add_constructor<LuaWorld, World*>();

    registry.register_cls<LuaEntity>()
        .add_method("id", &LuaEntity::id)
        .add_method("component", &LuaEntity::component)
        .add_constructor<LuaEntity, World*, Entity>();
}

LuaRuntime make_test_runtime() {
    register_script_test_metadata();

    LuaRuntime runtime;
    runtime.register_type(type<ScriptTestError>());
    runtime.register_type(type<ScriptTestReceiver>());
    runtime.register_enum(Registry::instance().get_enum<ScriptTestEnum>());
    return runtime;
}

} // namespace

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
    "run_script_components reloads modified script assets",
    "[scripting][lua][reload]"
) {
    register_script_test_metadata();
    register_world_script_metadata();

    World world;
    auto& runtime = world.add_resource(LuaRuntime {});
    runtime.register_type(type<ScriptTestReceiver>());
    runtime.register_type(type<LuaWorld>());
    runtime.register_type(type<LuaEntity>());

    world.add_resource(Events<AssetEvent<ScriptAsset>> {});
    auto& scripts = world.add_resource(Assets<ScriptAsset>(nullptr));
    auto& receiver = world.add_resource(ScriptTestReceiver {});

    auto script = scripts.emplace(R"(
        function on_update(entity, world)
            local receiver = world:resource(ScriptTestReceiver)
            receiver.value = receiver.value + 1
        end
    )");
    auto entity = world.entity();
    world.add_component(entity, ScriptComponent {.script = script});

    world.run_system_once(run_script_components);
    REQUIRE(receiver.value == 1);

    auto script_asset = scripts.get(script);
    REQUIRE(script_asset);
    *script_asset = ScriptAsset(R"(
        function on_update(entity, world)
            local receiver = world:resource(ScriptTestReceiver)
            receiver.value = receiver.value + 10
        end
    )");
    world.run_system_once(Assets<ScriptAsset>::track_assets);
    world.run_system_once(run_script_components);

    REQUIRE(receiver.value == 11);
}

TEST_CASE(
    "run_script_components unloads removed script assets",
    "[scripting][lua][reload]"
) {
    register_script_test_metadata();
    register_world_script_metadata();

    World world;
    auto& runtime = world.add_resource(LuaRuntime {});
    runtime.register_type(type<ScriptTestReceiver>());
    runtime.register_type(type<LuaWorld>());
    runtime.register_type(type<LuaEntity>());

    world.add_resource(Events<AssetEvent<ScriptAsset>> {});
    auto& scripts = world.add_resource(Assets<ScriptAsset>(nullptr));
    auto& receiver = world.add_resource(ScriptTestReceiver {});

    auto script = scripts.emplace(R"(
        function on_update(entity, world)
            local receiver = world:resource(ScriptTestReceiver)
            receiver.value = receiver.value + 1
        end
    )");
    auto entity = world.entity();
    world.add_component(entity, ScriptComponent {.script = script});

    world.run_system_once(run_script_components);
    REQUIRE(receiver.value == 1);
    REQUIRE(world.get_component<ScriptComponent>(entity).module.has_value());

    scripts.unload(script.id());
    world.run_system_once(Assets<ScriptAsset>::track_assets);
    world.run_system_once(run_script_components);

    REQUIRE(receiver.value == 1);
    const auto& script_comp = world.get_component<ScriptComponent>(entity);
    REQUIRE_FALSE(script_comp.module.has_value());
    REQUIRE(script_comp.loaded_script == invalid_asset_id);
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
    runtime.register_type(type<Transform3d>());

    Transform3d transform;
    transform.position = {1.0f, 2.0f, 3.0f};
    transform.rotation = {0.0f, 90.0f, 0.0f};

    runtime.set_global("transform", make_ref(transform));
    runtime.run_script(R"(
        local position = transform.position
        transform.position = position + transform:forward() * 10.0 * 0.5
        transform.rotation.x = transform.rotation.x + 90.0 * 0.5
    )");

    REQUIRE(transform.position.x == Catch::Approx(-4.0f));
    REQUIRE(transform.position.y == Catch::Approx(2.0f));
    REQUIRE(transform.position.z == Catch::Approx(3.0f));
    REQUIRE(transform.rotation.x == Catch::Approx(45.0f));
    REQUIRE(transform.rotation.y == Catch::Approx(90.0f));
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
