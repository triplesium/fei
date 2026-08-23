#include "scripting/reflection_bridge.hpp"

#include "refl/cls.hpp"
#include "refl/constructor.hpp"
#include "refl/method.hpp"
#include "refl/registry.hpp"
#include "refl/type.hpp"

#include <algorithm>
#include <string>

namespace ets {
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

bool is_script_visible(const Type& type) {
    return type.has_structured_name() && !type.has_annotation("NoScript");
}

ScriptTypeName script_type_name(const Type& type) {
    auto namespace_path = type.namespace_path();
    if (!namespace_path.empty() && namespace_path.front() == "ets") {
        namespace_path = namespace_path.subspan(1);
    }
    return {
        .namespace_path = namespace_path,
        .local_name = type.local_name(),
    };
}

std::string script_type_path(const Type& type, std::string_view separator) {
    const auto name = script_type_name(type);
    std::string result;
    for (const auto& component : name.namespace_path) {
        if (!result.empty()) {
            result += separator;
        }
        result += component;
    }
    if (!result.empty()) {
        result += separator;
    }
    result += name.local_name;
    return result;
}

Result<Val, InvokeFailure> script_default_construct(TypeId type_id) {
    auto type = Registry::instance().try_get_type(type_id);
    if (!type) {
        return failure(InvokeFailure::invalid_call(type.error().message));
    }
    if (!type->default_constructible()) {
        return failure(
            InvokeFailure::invalid_call(
                "Type '" + type->name() +
                "' does not have a matching constructor"
            )
        );
    }
    return Val::default_construct(*type);
}

Result<Val, InvokeFailure>
script_construct(TypeId type_id, const std::vector<Ref>& arguments) {
    auto cls = Registry::instance().try_get_cls(type_id);
    if (!cls) {
        return failure(InvokeFailure::invalid_call(cls.error().message));
    }
    auto constructor = cls->get_constructor_for_args(arguments);
    if (!constructor) {
        if (arguments.empty()) {
            return script_default_construct(type_id);
        }
        return failure(std::move(constructor.error()));
    }
    auto result = constructor->invoke_variadic(arguments);
    if (!result) {
        return failure(std::move(result.error()));
    }
    if (!result->is_value()) {
        return failure(
            InvokeFailure::invalid_call("Constructor returned an invalid value")
        );
    }
    return std::move(result->value());
}

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

bool script_has_static_method(TypeId type, std::string_view name) {
    auto cls = Registry::instance().try_get_cls(type);
    if (!cls) {
        return false;
    }
    const auto methods = cls->get_methods(std::string {name});
    return std::ranges::any_of(methods, [](const Method* method) {
        return method != nullptr && method->is_static();
    });
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

InvokeResult script_invoke_static_method(
    TypeId type,
    std::string_view name,
    const std::vector<Ref>& arguments
) {
    auto cls = Registry::instance().try_get_cls(type);
    if (!cls) {
        return failure(InvokeFailure::invalid_call(cls.error().message));
    }

    Method* best = nullptr;
    int best_score = 0;
    bool ambiguous = false;
    for (auto* method : cls->get_methods(std::string {name})) {
        if (method == nullptr || !method->is_static()) {
            continue;
        }
        const auto score = method->match_score(arguments);
        if (!score) {
            continue;
        }
        if (best == nullptr || *score < best_score) {
            best = method;
            best_score = *score;
            ambiguous = false;
        } else if (*score == best_score) {
            ambiguous = true;
        }
    }

    auto reflected_type = Registry::instance().try_get_type(type);
    const auto type_name =
        reflected_type ? reflected_type->name() : std::to_string(type.id());
    if (ambiguous) {
        return failure(
            InvokeFailure::invalid_call(
                "Ambiguous static method '" + type_name + "." +
                std::string {name} + "'"
            )
        );
    }
    if (best == nullptr) {
        return failure(
            InvokeFailure::invalid_call(
                "No matching static method '" + type_name + "." +
                std::string {name} + "' found"
            )
        );
    }
    return best->invoke_variadic(arguments);
}

} // namespace ets
