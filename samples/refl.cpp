#include "generated/reflgen.hpp"
#include "math/vector.hpp"
#include "refl/cls.hpp"
#include "refl/registry.hpp"

#include <print>

using namespace fei;

int main() {
    register_classes();
    {
        auto& cls = Registry::instance().get_cls<Vector3>();
        auto* property_x = cls.get_property("x");
        Vector3 vec {1.0f, 2.0f, 3.0f};
        std::println("Vector3 x: {}", property_x->get(vec).get<float>());
        auto* method_mag = cls.get_methods("magnitude").front();
        auto mag = method_mag->invoke(vec).ref().get<float>();
        std::println("Vector3 magnitude: {}", mag);
    }
    return 0;
}
