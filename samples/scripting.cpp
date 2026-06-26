#include "math/vector.hpp"
#include "refl/generated.hpp"
#include "refl/ref_utils.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "scripting_lua/runtime.hpp"
#include "window/input.hpp"

#include <print>

int main() {
    using namespace fei;
    register_generated_reflection();
    LuaRuntime runtime;
    runtime.register_type(type<Vector2>());
    runtime.register_type(type<KeyInput>());
    runtime.register_enum(Registry::instance().get_enum<KeyCode>());
    std::println(
        "Registered Vector2 with type id {}",
        Registry::instance().get_type<Vector2>().id().id()
    );
    auto script = R"(
        local vec = Vector2.new(1.0, 2.0)
        print(Vector2.__type_id)
        print(vec.x, vec.y)
        vec.x = 3.0
        vec.y = 4.0
        print(vec.x, vec.y)
        vec:set(6.0, 8.0)
        print(vec.x, vec.y)
        print("Magnitude:", vec:magnitude())
        local vec2 = Vector2.new(5.0, 6.0)
        print("Dot product:", vec:dot(vec2))
    )";
    runtime.run_script(script);

    Vector2 v(1.0f, 2.0f);
    KeyInput input;
    runtime.set_global("v", make_ref(v));
    runtime.set_global("input", make_ref(input));
    runtime.run_script(R"(
        print(v.x, v.y)
        v.x = 5.0
        v.y = 6.0
        print(v.x, v.y)
        print(input:just_pressed(KeyCode.Space))
    )");
    runtime.unset_global("v");
    std::println("v after script: {}, {}", v.x, v.y);

    return 0;
}
