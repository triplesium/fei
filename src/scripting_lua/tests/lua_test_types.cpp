#include "scripting_lua/tests/lua_test_types.hpp"

#include "core/transform.hpp"
#include "math/quaternion.hpp"
#include "math/vector.hpp"
#include "refl/cls.hpp"
#include "refl/enum.hpp"
#include "refl/registry.hpp"

#include <cstdint>
#include <string_view>

namespace fei {

ScriptTestReceiver::ScriptTestReceiver() = default;

ScriptTestReceiver::ScriptTestReceiver(int value) : value(value) {}

void ScriptTestReceiver::set_value(int next) {
    value = next;
    ++method_calls;
}

void ScriptTestReceiver::set_mode(ScriptTestEnum next) {
    mode = next;
    ++method_calls;
}

void ScriptTestReceiver::copy_to(ScriptTestReceiver& target) const {
    target.mode = mode;
    target.value = value;
}

void ScriptTestReceiver::set_scaled(float factor) {
    value = static_cast<int>(factor * 2.0f);
    ++method_calls;
}

void ScriptTestReceiver::set_text_size(std::string_view text) {
    value = static_cast<int>(text.size());
    ++method_calls;
}

void ScriptTestReceiver::choose_number(float) {
    value = 1;
    ++method_calls;
}

void ScriptTestReceiver::choose_number(double) {
    value = 2;
    ++method_calls;
}

Result<int, ScriptTestError>
ScriptTestReceiver::result_value(bool succeed) const {
    if (succeed) {
        return 42;
    }
    return failure(ScriptTestError {.code = 7});
}

Status<ScriptTestError> ScriptTestReceiver::status_value(bool succeed) const {
    if (succeed) {
        return {};
    }
    return failure(ScriptTestError {.code = 9});
}

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

    registry.register_cls<Quaternion>()
        .add_property("x", &Quaternion::x)
        .add_property("y", &Quaternion::y)
        .add_property("z", &Quaternion::z)
        .add_property("w", &Quaternion::w)
        .add_constructor<Quaternion>()
        .add_constructor<Quaternion, float, float, float, float>();

    registry.register_cls<Transform3d>()
        .add_property("position", &Transform3d::position)
        .add_property("rotation", &Transform3d::rotation)
        .add_property("scale", &Transform3d::scale)
        .add_method("set_euler", &Transform3d::set_euler)
        .add_method("rotate", &Transform3d::rotate)
        .add_method("rotate_axis", &Transform3d::rotate_axis)
        .add_method("rotate_x", &Transform3d::rotate_x)
        .add_method("rotate_y", &Transform3d::rotate_y)
        .add_method("rotate_z", &Transform3d::rotate_z)
        .add_method("forward", &Transform3d::forward)
        .add_method("right", &Transform3d::right)
        .add_method("up", &Transform3d::up);
}

LuaRuntime make_test_runtime() {
    register_script_test_metadata();

    LuaRuntime runtime;
    runtime.bind_type(type<ScriptTestError>());
    runtime.bind_type(type<ScriptTestReceiver>());
    runtime.bind_enum(Registry::instance().get_enum<ScriptTestEnum>());
    return runtime;
}

} // namespace fei
