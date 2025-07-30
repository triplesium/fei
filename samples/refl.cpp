#include <print>

#include "generated/reflgen.hpp"
#include "math/vector.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"

using namespace fei;

int main() {
    register_classes();
    {
        auto& cls = Registry::instance().get_cls<Vector2>();
        auto& property_x = cls.get_property("x");
        Vector2 vec {1.0f, 2.0f};
        std::println("Vector2 x: {}", property_x.get(vec).get<float>());
        auto& method_mag = cls.get_method("magnitude");
        auto mag = method_mag.invoke(vec).ref().get<float>();
        std::println("Vector2 magnitude: {}", mag);
    }
    return 0;
}
