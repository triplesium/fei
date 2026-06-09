#include "refl/cls.hpp"
#include "refl/registry.hpp"

namespace fei {

Property* Cls::get_property(const std::string& name) {
    auto it = m_properties.find(name);
    if (it != m_properties.end()) {
        return it->second.get();
    }
    return nullptr;
}

Method* Cls::get_method(
    const std::string& name,
    std::vector<TypeId> arg_types,
    MethodConstFilter const_filter
) {
    auto it = m_methods.find(name);
    if (it == m_methods.end()) {
        return nullptr;
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

    auto matches_const_filter =
        [](const Method& method, MethodConstFilter filter) {
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
            return find_matching(const_filter);
        case MethodConstFilter::PreferConst:
            if (auto* method = find_matching(MethodConstFilter::ConstOnly)) {
                return method;
            }
            return find_matching(MethodConstFilter::NonConstOnly);
        case MethodConstFilter::PreferNonConst:
            if (auto* method = find_matching(MethodConstFilter::NonConstOnly)) {
                return method;
            }
            return find_matching(MethodConstFilter::ConstOnly);
    }
    return nullptr;
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

Constructor* Cls::get_constructor(const std::vector<TypeId>& arg_types) {
    for (const auto& ctor : m_constructors) {
        if (ctor->arg_types() == arg_types) {
            return ctor.get();
        }
    }
    return nullptr;
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
