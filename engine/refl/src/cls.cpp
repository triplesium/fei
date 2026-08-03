#include "refl/cls.hpp"

#include "base/log.hpp"
#include "refl/registry.hpp"

#include <string>
#include <utility>

namespace fei {

namespace {

Optional<std::string> registered_type_name(TypeId id) {
    auto type = Registry::instance().try_get_type(id);
    if (!type) {
        return nullopt;
    }
    return type->name();
}

std::string describe_type(TypeId id) {
    auto type = Registry::instance().try_get_type(id);
    if (type) {
        return type->name();
    }
    return "id " + std::to_string(id.id());
}

std::string
describe_owner(TypeId id, const Optional<std::string>& owner_type_name) {
    if (owner_type_name) {
        return "'" + *owner_type_name + "'";
    }
    return "id " + std::to_string(id.id());
}

std::string describe_args(const std::vector<TypeId>& arg_types) {
    std::string args;
    for (std::size_t i = 0; i < arg_types.size(); ++i) {
        if (i > 0) {
            args += ", ";
        }
        args += describe_type(arg_types[i]);
    }
    return args;
}

} // namespace

ClsError ClsError::property_not_found(TypeId owner_id, std::string name) {
    auto owner_name = registered_type_name(owner_id);
    auto message = "Property '" + name + "' not found in class " +
                   describe_owner(owner_id, owner_name);
    return ClsError {
        .kind = Kind::PropertyNotFound,
        .owner_type_id = owner_id,
        .owner_type_name = std::move(owner_name),
        .member_name = std::move(name),
        .arg_types = {},
        .message = std::move(message),
    };
}

ClsError ClsError::method_not_found(
    TypeId owner_id,
    std::string name,
    std::vector<TypeId> arg_types
) {
    auto owner_name = registered_type_name(owner_id);
    auto message = "Method '" + name + "(" + describe_args(arg_types) +
                   ")' not found in class " +
                   describe_owner(owner_id, owner_name);
    return ClsError {
        .kind = Kind::MethodNotFound,
        .owner_type_id = owner_id,
        .owner_type_name = std::move(owner_name),
        .member_name = std::move(name),
        .arg_types = std::move(arg_types),
        .message = std::move(message),
    };
}

ClsError ClsError::constructor_not_found(
    TypeId owner_id,
    std::vector<TypeId> arg_types
) {
    auto owner_name = registered_type_name(owner_id);
    auto message = "Constructor (" + describe_args(arg_types) +
                   ") not found for class " +
                   describe_owner(owner_id, owner_name);
    return ClsError {
        .kind = Kind::ConstructorNotFound,
        .owner_type_id = owner_id,
        .owner_type_name = std::move(owner_name),
        .member_name = {},
        .arg_types = std::move(arg_types),
        .message = std::move(message),
    };
}

Property& Cls::get_property(const std::string& name) {
    auto result = try_get_property(name);
    if (!result) {
        fatal("{}", result.error().message);
    }
    return *result;
}

Result<Property&, ClsError> Cls::try_get_property(const std::string& name) {
    auto it = m_properties.find(name);
    if (it != m_properties.end()) {
        return *it->second;
    }
    return failure(ClsError::property_not_found(m_type_id, name));
}

Cls& Cls::add_method(std::unique_ptr<Method> method) {
    if (!method) {
        fatal("Cannot add a null method to class {}", describe_type(m_type_id));
    }
    auto& methods = m_methods[method->name()];
    auto is_duplicate = [&](const std::unique_ptr<Method>& existing) {
        if (existing->params().size() != method->params().size()) {
            return false;
        }
        for (std::size_t i = 0; i < method->params().size(); ++i) {
            if (existing->params()[i].type() != method->params()[i].type()) {
                return false;
            }
        }
        return existing->return_type() == method->return_type() &&
               existing->is_const() == method->is_const() &&
               existing->is_static() == method->is_static();
    };
    if (std::ranges::none_of(methods, is_duplicate)) {
        methods.push_back(std::move(method));
    }
    return *this;
}

Method& Cls::get_method(
    const std::string& name,
    std::vector<TypeId> arg_types,
    MethodConstFilter const_filter
) {
    auto result = try_get_method(name, std::move(arg_types), const_filter);
    if (!result) {
        fatal("{}", result.error().message);
    }
    return *result;
}

Result<Method&, ClsError> Cls::try_get_method(
    const std::string& name,
    std::vector<TypeId> arg_types,
    MethodConstFilter const_filter
) {
    auto it = m_methods.find(name);
    if (it == m_methods.end()) {
        return failure(
            ClsError::method_not_found(m_type_id, name, std::move(arg_types))
        );
    }

    auto matches_args = [&](const Method& method) {
        if (method.params().size() != arg_types.size()) {
            return false;
        }
        for (std::size_t i = 0; i < method.params().size(); ++i) {
            auto param_type = method.params()[i].type_id();
            auto arg_type = arg_types[i];
            if (param_type != arg_type) {
                return false;
            }
        }
        return true;
    };

    auto matches_const_filter = [](const Method& method,
                                   MethodConstFilter filter) {
        switch (filter) {
            case MethodConstFilter::Any:
                return true;
            case MethodConstFilter::ConstOnly:
                return method.is_const();
            case MethodConstFilter::NonConstOnly:
                return !method.is_const();
            case MethodConstFilter::PreferConst:
            case MethodConstFilter::PreferNonConst:
                return true;
        }
        return true;
    };

    auto find_matching = [&](MethodConstFilter filter) -> Method* {
        for (const auto& method : it->second) {
            if (!matches_const_filter(*method, filter)) {
                continue;
            }
            if (matches_args(*method)) {
                return method.get();
            }
        }
        return nullptr;
    };

    switch (const_filter) {
        case MethodConstFilter::Any:
        case MethodConstFilter::ConstOnly:
        case MethodConstFilter::NonConstOnly:
            if (auto* method = find_matching(const_filter)) {
                return *method;
            }
            break;
        case MethodConstFilter::PreferConst:
            if (auto* method = find_matching(MethodConstFilter::ConstOnly)) {
                return *method;
            }
            if (auto* method = find_matching(MethodConstFilter::NonConstOnly)) {
                return *method;
            }
            break;
        case MethodConstFilter::PreferNonConst:
            if (auto* method = find_matching(MethodConstFilter::NonConstOnly)) {
                return *method;
            }
            if (auto* method = find_matching(MethodConstFilter::ConstOnly)) {
                return *method;
            }
            break;
    }
    return failure(
        ClsError::method_not_found(m_type_id, name, std::move(arg_types))
    );
}

Result<Method&, InvokeFailure> Cls::get_method_for_args(
    const std::string& name,
    const std::vector<Ref>& args,
    MethodConstFilter const_filter
) {
    auto it = m_methods.find(name);
    if (it == m_methods.end()) {
        return failure(
            InvokeFailure::invalid_call(
                "No matching method '" + describe_type(m_type_id) + "." + name +
                "' found"
            )
        );
    }

    struct Match {
        Method* method {nullptr};
        bool ambiguous {false};
    };

    auto matches_const_filter = [](const Method& method,
                                   MethodConstFilter filter) {
        switch (filter) {
            case MethodConstFilter::Any:
                return true;
            case MethodConstFilter::ConstOnly:
                return method.is_const();
            case MethodConstFilter::NonConstOnly:
                return !method.is_const();
            case MethodConstFilter::PreferConst:
            case MethodConstFilter::PreferNonConst:
                return true;
        }
        return true;
    };

    auto find_best = [&](MethodConstFilter filter) -> Match {
        Match best;
        int best_score = 0;
        for (const auto& method : it->second) {
            if (!matches_const_filter(*method, filter)) {
                continue;
            }
            auto score = method->match_score(args);
            if (!score) {
                continue;
            }
            if (best.method == nullptr || *score < best_score) {
                best.method = method.get();
                best.ambiguous = false;
                best_score = *score;
            } else if (*score == best_score) {
                best.ambiguous = true;
            }
        }
        return best;
    };

    auto finish = [&](Match match) -> Result<Method&, InvokeFailure> {
        if (match.ambiguous) {
            return failure(
                InvokeFailure::invalid_call(
                    "Ambiguous method '" + describe_type(m_type_id) + "." +
                    name + "'"
                )
            );
        }
        if (match.method != nullptr) {
            return *match.method;
        }
        return failure(
            InvokeFailure::invalid_call(
                "No matching method '" + describe_type(m_type_id) + "." + name +
                "' found"
            )
        );
    };

    switch (const_filter) {
        case MethodConstFilter::Any:
        case MethodConstFilter::ConstOnly:
        case MethodConstFilter::NonConstOnly:
            return finish(find_best(const_filter));
        case MethodConstFilter::PreferConst: {
            auto preferred = find_best(MethodConstFilter::ConstOnly);
            if (preferred.ambiguous || preferred.method != nullptr) {
                return finish(preferred);
            }
            return finish(find_best(MethodConstFilter::NonConstOnly));
        }
        case MethodConstFilter::PreferNonConst: {
            auto preferred = find_best(MethodConstFilter::NonConstOnly);
            if (preferred.ambiguous || preferred.method != nullptr) {
                return finish(preferred);
            }
            return finish(find_best(MethodConstFilter::ConstOnly));
        }
    }
    return finish({});
}

bool Cls::has_method(const std::string& name) const {
    return m_methods.contains(name);
}

std::vector<Method*> Cls::get_methods() const {
    std::vector<Method*> methods;
    for (const auto& [name, method_list] : m_methods) {
        for (const auto& method : method_list) {
            methods.push_back(method.get());
        }
    }
    return methods;
}

std::vector<Method*> Cls::get_methods(const std::string& name) const {
    std::vector<Method*> methods;
    auto it = m_methods.find(name);
    if (it != m_methods.end()) {
        for (const auto& method : it->second) {
            methods.push_back(method.get());
        }
    }
    return methods;
}

Constructor& Cls::get_constructor(const std::vector<TypeId>& arg_types) {
    auto result = try_get_constructor(arg_types);
    if (!result) {
        fatal("{}", result.error().message);
    }
    return *result;
}

Result<Constructor&, ClsError>
Cls::try_get_constructor(std::vector<TypeId> arg_types) {
    for (const auto& ctor : m_constructors) {
        if (ctor->arg_types() == arg_types) {
            return *ctor;
        }
    }
    return failure(
        ClsError::constructor_not_found(m_type_id, std::move(arg_types))
    );
}

Result<Constructor&, InvokeFailure>
Cls::get_constructor_for_args(const std::vector<Ref>& args) {
    Constructor* best = nullptr;
    int best_score = 0;
    bool ambiguous = false;

    for (const auto& ctor : m_constructors) {
        auto score = ctor->match_score(args);
        if (!score) {
            continue;
        }
        if (best == nullptr || *score < best_score) {
            best = ctor.get();
            best_score = *score;
            ambiguous = false;
        } else if (*score == best_score) {
            ambiguous = true;
        }
    }
    if (ambiguous) {
        return failure(
            InvokeFailure::invalid_call(
                "Ambiguous constructor for " + describe_type(m_type_id)
            )
        );
    }
    if (best != nullptr) {
        return *best;
    }
    return failure(
        InvokeFailure::invalid_call(
            "No matching constructor found for " + describe_type(m_type_id)
        )
    );
}

std::vector<Constructor*> Cls::get_constructors() const {
    std::vector<Constructor*> constructors;
    constructors.reserve(m_constructors.size());
    for (const auto& constructor : m_constructors) {
        constructors.push_back(constructor.get());
    }
    return constructors;
}

Cls& Cls::set_to_string(ToStringFunc func) {
    m_to_string_func = func;
    return *this;
}

std::string Cls::to_string(Ref ref) const {
    if (m_to_string_func) {
        return m_to_string_func(ref);
    }
    return type_name(m_type_id);
}

std::vector<Property*> Cls::get_properties() const {
    std::vector<Property*> props;
    props.reserve(m_properties.size());
    for (const auto& pair : m_properties) {
        props.push_back(pair.second.get());
    }
    return props;
}

} // namespace fei
