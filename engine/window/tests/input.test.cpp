#include "window/input.hpp"

#include "ecs/world.hpp"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace fei;

TEST_CASE("Key codes convert from reflected names", "[window][input]") {
    CHECK(key_code_from_string("A") == KeyCode::A);
    CHECK(key_code_from_string("Space") == KeyCode::Space);
    CHECK(key_code_from_string("not-a-key") == KeyCode::Unknown);
}

TEST_CASE("Virtual input replaces its pressed key set", "[window][input]") {
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

TEST_CASE("Exclusive virtual input replaces physical keys", "[window][input]") {
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
