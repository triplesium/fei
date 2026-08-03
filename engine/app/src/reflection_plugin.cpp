#include "app/reflection_plugin.hpp"

#include "app/app.hpp"
#include "refl/generated.hpp"

namespace fei {

void ReflectionPlugin::setup(App&) {
    register_generated_reflection();
}

} // namespace fei
