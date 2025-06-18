#include "refl/registry.hpp"

namespace fei {

Registry* Registry::s_instance = nullptr;
Registry& Registry::instance() {
    if (!s_instance) {
        s_instance = new Registry();
    }
    return *s_instance;
}

} // namespace fei
