#include "ecs/dynamic/resource.hpp"

#include "ecs/annotations.hpp"
#include "ecs/world.hpp"
#include "refl/registry.hpp"

#include <utility>

namespace ets {

DynamicResourceParam::DynamicResourceParam(
    std::string name,
    TypeId type,
    DynamicParamAccess access,
    bool optional
) :
    name(std::move(name)), type(type), param_access(access),
    optional(optional) {}

SystemAccess DynamicResourceParam::access() const {
    SystemAccess result;
    if (param_access == DynamicParamAccess::Write) {
        result.write_resources.insert(type);
    } else {
        result.read_resources.insert(type);
    }
    if (auto reflected_type = Registry::instance().try_get_type(type)) {
        const auto resource =
            reflected_type->annotation<annotations::Resource>();
        result.main_thread_only = resource && resource->main_thread_only;
    }
    return result;
}

Result<Ref, DynamicSystemError>
DynamicResourceParam::prepare(World& world, SystemTicks system_ticks) {
    (void)system_ticks;
    if (!world.has_resource(type)) {
        if (optional) {
            return Ref {};
        }
        return failure(
            DynamicSystemError {"missing resource '" + type_name(type) + "'"}
        );
    }

    if (param_access == DynamicParamAccess::Write) {
        return world.resource(type);
    }

    return static_cast<const World&>(world).resource(type);
}

} // namespace ets
