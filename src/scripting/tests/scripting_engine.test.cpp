#include "scripting/scripting_engine.hpp"

#include "base/result.hpp"
#include "core/transform.hpp"
#include "ecs/world.hpp"
#include "math/vector.hpp"
#include "refl/cls.hpp"
#include "refl/enum.hpp"
#include "refl/ref_utils.hpp"
#include "refl/registry.hpp"
#include "scripting/entity.hpp"
#include "scripting/world.hpp"

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

ScriptingEngine make_test_engine() {
    register_script_test_metadata();

    ScriptingEngine engine;
    engine.register_type(type<ScriptTestError>());
    engine.register_type(type<ScriptTestReceiver>());
    engine.register_enum(Registry::instance().get_enum<ScriptTestEnum>());
    return engine;
}

} // namespace

TEST_CASE(
    "ScriptingEngine reads properties, writes properties, and calls methods",
    "[scripting]"
) {
    auto engine = make_test_engine();
    ScriptTestReceiver receiver;
    receiver.value = 4;

    engine.set_global("receiver", make_ref(receiver));
    engine.run_script(R"(
        receiver.value = receiver.value + 3
        receiver:set_value(receiver.value + 5)
    )");

    REQUIRE(receiver.value == 12);
    REQUIRE(receiver.method_calls == 1);
}

TEST_CASE(
    "ScriptingEngine constructs reflected objects from Lua",
    "[scripting]"
) {
    auto engine = make_test_engine();
    ScriptTestReceiver receiver;

    engine.set_global("receiver", make_ref(receiver));
    engine.run_script(R"(
        local created = ScriptTestReceiver.new(21)
        created.mode = ScriptTestEnum.Active
        created:copy_to(receiver)
    )");

    REQUIRE(receiver.value == 21);
    REQUIRE(receiver.mode == ScriptTestEnum::Active);
}

TEST_CASE(
    "ScriptingEngine calls Lua functions with reflected refs",
    "[scripting]"
) {
    auto engine = make_test_engine();
    ScriptTestReceiver receiver;
    receiver.value = 10;

    engine.run_script(R"(
        function update_receiver(target)
            target.value = target.value + 8
            target:set_mode(ScriptTestEnum.Active)
        end
    )");

    REQUIRE(engine.call_function(
        "update_receiver",
        std::vector<Ref> {make_ref(receiver)}
    ));
    REQUIRE(receiver.value == 18);
    REQUIRE(receiver.mode == ScriptTestEnum::Active);
    REQUIRE(receiver.method_calls == 1);
}

TEST_CASE(
    "ScriptingEngine passes enum constants to reflected methods",
    "[scripting][enum]"
) {
    auto engine = make_test_engine();
    ScriptTestReceiver receiver;

    engine.set_global("receiver", make_ref(receiver));
    engine.run_script("receiver:set_mode(ScriptTestEnum.Active)");

    REQUIRE(receiver.mode == ScriptTestEnum::Active);
    REQUIRE(receiver.method_calls == 1);
}

TEST_CASE(
    "ScriptingEngine assigns enum constants to reflected properties",
    "[scripting][enum]"
) {
    auto engine = make_test_engine();
    ScriptTestReceiver receiver;

    engine.set_global("receiver", make_ref(receiver));
    engine.run_script("receiver.mode = ScriptTestEnum.Active");

    REQUIRE(receiver.mode == ScriptTestEnum::Active);
    REQUIRE(receiver.method_calls == 0);
}

TEST_CASE(
    "ScriptingEngine applies reflected property conversions",
    "[scripting]"
) {
    auto engine = make_test_engine();
    ScriptTestReceiver receiver;

    engine.set_global("receiver", make_ref(receiver));
    engine.run_script(R"(
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
    "ScriptingEngine supports nested reflected transform updates",
    "[scripting]"
) {
    register_transform_script_metadata();

    ScriptingEngine engine;
    engine.register_type(type<Vector3>());
    engine.register_type(type<Transform3d>());

    Transform3d transform;
    transform.position = {1.0f, 2.0f, 3.0f};
    transform.rotation = {0.0f, 90.0f, 0.0f};

    engine.set_global("transform", make_ref(transform));
    engine.run_script(R"(
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
    "ScriptingEngine passes Lua type tables as reflected TypeId arguments",
    "[scripting]"
) {
    register_script_test_metadata();
    register_transform_script_metadata();
    register_world_script_metadata();

    ScriptingEngine engine;
    engine.register_type(type<ScriptTestReceiver>());
    engine.register_type(type<Vector3>());
    engine.register_type(type<Transform3d>());
    engine.register_type(type<LuaWorld>());
    engine.register_type(type<LuaEntity>());

    World world;
    auto& receiver = world.add_resource(ScriptTestReceiver {});
    receiver.value = 5;

    auto entity_id = world.entity();
    Transform3d transform;
    transform.position = {1.0f, 2.0f, 3.0f};
    world.add_component(entity_id, transform);

    LuaWorld lua_world(&world);
    LuaEntity lua_entity(&world, entity_id);
    engine.set_global("world", make_ref(lua_world));
    engine.set_global("entity", make_ref(lua_entity));

    engine.run_script(R"(
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
    "ScriptingEngine applies reflected weak argument conversions",
    "[scripting]"
) {
    auto engine = make_test_engine();
    ScriptTestReceiver receiver;

    engine.set_global("receiver", make_ref(receiver));
    engine.run_script(R"(
        receiver:set_scaled(4)
        receiver:set_text_size("hello")
    )");

    REQUIRE(receiver.value == 5);
    REQUIRE(receiver.method_calls == 2);
}

TEST_CASE(
    "ScriptingEngine rejects ambiguous reflected overloads",
    "[scripting]"
) {
    auto engine = make_test_engine();
    ScriptTestReceiver receiver;

    engine.set_global("receiver", make_ref(receiver));
    engine.run_script(R"(
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

TEST_CASE("ScriptingEngine maps reflected Result returns", "[scripting]") {
    auto engine = make_test_engine();
    ScriptTestReceiver receiver;

    engine.set_global("receiver", make_ref(receiver));
    engine.run_script(R"(
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

TEST_CASE("ScriptingEngine maps reflected Status returns", "[scripting]") {
    auto engine = make_test_engine();
    ScriptTestReceiver receiver;

    engine.set_global("receiver", make_ref(receiver));
    engine.run_script(R"(
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
    "ScriptingEngine raises Lua errors for invalid reflected calls",
    "[scripting]"
) {
    auto engine = make_test_engine();
    ScriptTestReceiver receiver;

    engine.set_global("receiver", make_ref(receiver));
    engine.run_script(R"(
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
