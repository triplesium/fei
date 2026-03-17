#include "app/app.hpp"
#include "generated/reflgen.hpp"
#include "math/vector.hpp"
#include "refl/ref_utils.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"
#include "scripting/plugin.hpp"
#include "scripting/scripting_engine.hpp"
#include "window/input.hpp"

#include <print>

int main() {
    using namespace fei;
    register_classes();
    ScriptingEngine engine;
    engine.register_type(type<Vector2>());
    engine.register_type(type<KeyInput>());
    engine.register_enum(Registry::instance().get_enum<KeyCode>());
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
    engine.run_script(script);

    Vector2 v(1.0f, 2.0f);
    KeyInput input;
    engine.set_global("v", make_ref(v));
    engine.set_global("input", make_ref(input));
    engine.run_script(R"(
        print(v.x, v.y)
        v.x = 5.0
        v.y = 6.0
        print(v.x, v.y)
        print(input:just_pressed(KeyCode.Space))
    )");
    engine.unset_global("v");
    std::println("v after script: {}, {}", v.x, v.y);

    return 0;
}
