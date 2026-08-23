#include "input/input.hpp"

#include "app/app.hpp"
#include "ecs/world.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace ets;

TEST_CASE("Key codes convert from reflected names", "[input]") {
    CHECK(key_code_from_string("A") == KeyCode::A);
    CHECK(key_code_from_string("Space") == KeyCode::Space);
    CHECK(key_code_from_string("not-a-key") == KeyCode::Unknown);
}

TEST_CASE("Key input preserves held state across frames", "[input]") {
    KeyInput input;
    input.press(KeyCode::A);
    CHECK(input.just_pressed(KeyCode::A));

    input.advance_frame();
    CHECK(input.pressed(KeyCode::A));
    CHECK_FALSE(input.just_pressed(KeyCode::A));

    input.release(KeyCode::A);
    CHECK(input.just_released(KeyCode::A));
    input.advance_frame();
    CHECK_FALSE(input.just_released(KeyCode::A));
}

TEST_CASE("Key input preserves both edges within one frame", "[input]") {
    KeyInput input;
    input.press(KeyCode::A);
    input.release(KeyCode::A);

    CHECK_FALSE(input.pressed(KeyCode::A));
    CHECK(input.just_pressed(KeyCode::A));
    CHECK(input.just_released(KeyCode::A));
}

TEST_CASE("InputPlugin converts events into frame state", "[input]") {
    App app;
    app.add_plugin<InputPlugin>();
    app.startup();

    app.resource<Events<KeyEvent>>().send(
        KeyEvent {.key_code = KeyCode::A, .state = KeyState::Pressed}
    );
    app.resource<Events<MouseButtonEvent>>().send(
        MouseButtonEvent {
            .button = MouseButton::Left,
            .state = KeyState::Pressed,
        }
    );
    app.resource<Events<MouseMoveEvent>>().send(
        MouseMoveEvent {.position = {12.0F, 34.0F}}
    );
    app.resource<Events<MouseScrollEvent>>().send(
        MouseScrollEvent {.delta = {1.0F, -2.0F}}
    );
    app.resource<Events<CharacterEvent>>().send(
        CharacterEvent {.character = U'\u754c'}
    );
    app.update();

    CHECK(app.resource<KeyInput>().just_pressed(KeyCode::A));
    CHECK(app.resource<MouseInput>().just_pressed(MouseButton::Left));
    CHECK(app.resource<MouseInput>().position() == Vector2 {12.0F, 34.0F});
    CHECK(app.resource<MouseScrollInput>().delta() == Vector2 {1.0F, -2.0F});
    REQUIRE(app.resource<CharacterInput>().characters().size() == 1);
    CHECK(app.resource<CharacterInput>().characters()[0] == U'\u754c');

    app.update();
    CHECK(app.resource<KeyInput>().pressed(KeyCode::A));
    CHECK_FALSE(app.resource<KeyInput>().just_pressed(KeyCode::A));
    CHECK(app.resource<MouseInput>().pressed(MouseButton::Left));
    CHECK(app.resource<MouseScrollInput>().delta() == Vector2::Zero);
    CHECK(app.resource<CharacterInput>().characters().empty());

    app.resource<Events<InputFocusLost>>().send(InputFocusLost {});
    app.update();
    CHECK(app.resource<KeyInput>().just_released(KeyCode::A));
    CHECK(app.resource<MouseInput>().just_released(MouseButton::Left));
    app.shutdown();
}

TEST_CASE("Virtual input replaces its pressed key set", "[input]") {
    VirtualInput input;
    CHECK_FALSE(input.exclusive());
    input.set_exclusive(true);
    CHECK(input.exclusive());

    const std::vector first {KeyCode::A, KeyCode::Space};
    input.set_pressed_keys(first);

    CHECK(input.pressed(KeyCode::A));
    CHECK(input.pressed(KeyCode::Space));
    CHECK_FALSE(input.pressed(KeyCode::D));

    const std::vector second {KeyCode::D, KeyCode::Unknown};
    input.set_pressed_keys(second);
    CHECK_FALSE(input.pressed(KeyCode::A));
    CHECK(input.pressed(KeyCode::D));
    CHECK_FALSE(input.pressed(KeyCode::Unknown));

    input.clear();
    CHECK_FALSE(input.pressed(KeyCode::D));
}

TEST_CASE("Exclusive virtual input replaces physical keys", "[input]") {
    World world;
    world.add_resource(KeyInput {});
    world.add_resource(VirtualInput {});
    world.resource<KeyInput>().press(KeyCode::D);

    auto& virtual_input = world.resource<VirtualInput>();
    virtual_input.set_exclusive(true);
    const std::vector keys {KeyCode::A};
    virtual_input.set_pressed_keys(keys);
    world.run_system_once(apply_virtual_key_input);

    CHECK(world.resource<KeyInput>().pressed(KeyCode::A));
    CHECK_FALSE(world.resource<KeyInput>().pressed(KeyCode::D));
}

TEST_CASE(
    "Exclusive virtual input replaces mouse position and buttons",
    "[input][mouse]"
) {
    World world;
    world.add_resource(MouseInput {});
    world.add_resource(VirtualInput {});
    auto& virtual_input = world.resource<VirtualInput>();
    virtual_input.set_exclusive(true);
    virtual_input.set_mouse_position({120.0F, 75.0F});
    const std::vector buttons {MouseButton::Left};
    virtual_input.set_pressed_mouse_buttons(buttons);

    world.run_system_once(apply_virtual_mouse_input);

    CHECK(world.resource<MouseInput>().position() == Vector2 {120.0F, 75.0F});
    CHECK(world.resource<MouseInput>().pressed(MouseButton::Left));
    CHECK_FALSE(world.resource<MouseInput>().pressed(MouseButton::Right));
}

TEST_CASE("CharacterInput stores a frame of Unicode input", "[input]") {
    CharacterInput input;
    input.push(U'A');
    input.push(U'\u754c');

    REQUIRE(input.characters().size() == 2);
    CHECK(input.characters()[0] == U'A');
    CHECK(input.characters()[1] == U'\u754c');

    input.clear();
    CHECK(input.characters().empty());
}
