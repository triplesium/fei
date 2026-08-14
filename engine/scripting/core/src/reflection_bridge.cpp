#include "scripting/reflection_bridge.hpp"

#include "refl/cls.hpp"
#include "refl/method.hpp"
#include "refl/registry.hpp"

#include <string>

namespace fei {
namespace {

Result<Cls&, InvokeFailure> script_class(Ref instance) {
    if (!instance) {
        return failure(
            InvokeFailure::invalid_call("Reflected call has no receiver")
        );
    }
    auto cls = Registry::instance().try_get_cls(instance.type_id());
    if (!cls) {
        return failure(InvokeFailure::invalid_call(cls.error().message));
    }
    return *cls;
}

} // namespace

Result<Ref, InvokeFailure>
script_get_property(Ref instance, std::string_view name) {
    auto cls = script_class(instance);
    if (!cls) {
        return failure(std::move(cls.error()));
    }
    auto property = cls->try_get_property(std::string {name});
    if (!property) {
        return failure(InvokeFailure::invalid_call(property.error().message));
    }
    return property->get(instance);
}

Status<InvokeFailure>
script_set_property(Ref instance, std::string_view name, Ref value) {
    auto cls = script_class(instance);
    if (!cls) {
        return failure(std::move(cls.error()));
    }
    auto property = cls->try_get_property(std::string {name});
    if (!property) {
        return failure(InvokeFailure::invalid_call(property.error().message));
    }
    return property->set(instance, value);
}

bool script_has_method(Ref instance, std::string_view name) {
    auto cls = script_class(instance);
    return cls && cls->has_method(std::string {name});
}

InvokeResult script_invoke_method(
    Ref instance,
    std::string_view name,
    const std::vector<Ref>& arguments
) {
    auto cls = script_class(instance);
    if (!cls) {
        return failure(std::move(cls.error()));
    }

    std::vector<Ref> invocation_arguments;
    invocation_arguments.reserve(arguments.size() + 1);
    invocation_arguments.push_back(instance);
    invocation_arguments
        .insert(invocation_arguments.end(), arguments.begin(), arguments.end());
    const auto const_filter = instance.is_const() ?
                                  MethodConstFilter::ConstOnly :
                                  MethodConstFilter::PreferNonConst;
    auto method = cls->get_method_for_args(
        std::string {name},
        invocation_arguments,
        const_filter
    );
    if (!method) {
        return failure(std::move(method.error()));
    }
    return method->invoke_variadic(invocation_arguments);
}

} // namespace fei
