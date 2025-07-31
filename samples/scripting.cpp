#include "math/vector.hpp"
#include "refl/registry.hpp"
#include "scripting/scripting_engine.hpp"

#include "generated/reflgen.hpp"

int main() {
    using namespace fei;
    register_classes();
    ScriptingEngine engine;
    engine.register_type(Registry::instance().register_type<Vector2>());
    auto script = R"(
        local vec = Vector2.new(1.0, 2.0)
        print(vec.x, vec.y)
        vec.x = 3.0
        vec.y = 4.0
        print(vec.x, vec.y)
        print("Magnitude:", vec:magnitude())
    )";
    engine.run_script(script);

    return 0;
}
