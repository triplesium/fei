#pragma once

#include "refl/constructor.hpp"
#include "refl/method.hpp"
#include "refl/property.hpp"
#include "refl/type.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fei {

class Cls {
  private:
    TypeId m_type_id;
    std::unordered_map<std::string, std::unique_ptr<Property>> m_properties;
    std::unordered_map<std::string, std::vector<std::unique_ptr<Method>>>
        m_methods;
    std::vector<std::unique_ptr<Constructor>> m_constructors;

    using ToStringFunc = std::string (*)(Ref);
    ToStringFunc m_to_string_func = nullptr;

  public:
    Cls(TypeId type_id) : m_type_id(type_id) {}

    Cls(const Cls&) = delete;
    Cls& operator=(const Cls&) = delete;

    Cls(Cls&&) noexcept = default;
    Cls& operator=(Cls&&) noexcept = default;

    template<typename P>
    Cls& add_property(std::string name, P member_ptr) {
        m_properties[name] =
            std::make_unique<PropertyImpl<P>>(name, member_ptr);
        return *this;
    }

    Property* get_property(const std::string& name) {
        auto it = m_properties.find(name);
        if (it != m_properties.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    template<typename P>
    Cls& add_method(std::string name, P method_ptr) {
        m_methods[name].push_back(
            std::make_unique<MethodImpl<P>>(name, method_ptr)
        );
        return *this;
    }

    Method* get_method(const std::string& name, std::vector<TypeId> arg_types) {
        auto it = m_methods.find(name);
        if (it != m_methods.end()) {
            // Find method with matching argument types
            for (const auto& method : it->second) {
                bool match = true;
                for (int i = 0; i < method->params().size(); ++i) {
                    if (method->params()[i].type_id() != arg_types[i]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    return method.get();
                }
            }
        }
        return nullptr;
    }

    bool has_method(const std::string& name) const {
        return m_methods.contains(name);
    }

    std::vector<Method*> get_methods() const {
        std::vector<Method*> methods;
        for (const auto& [name, method_list] : m_methods) {
            for (const auto& method : method_list) {
                methods.push_back(method.get());
            }
        }
        return methods;
    }

    std::vector<Method*> get_methods(const std::string& name) const {
        std::vector<Method*> methods;
        auto it = m_methods.find(name);
        if (it != m_methods.end()) {
            for (const auto& method : it->second) {
                methods.push_back(method.get());
            }
        }
        return methods;
    }

    template<typename T, typename... Args>
        requires std::constructible_from<T, Args...>
    Cls& add_constructor() {
        m_constructors.push_back(std::make_unique<ConstructorImpl<T, Args...>>()
        );
        return *this;
    }

    Constructor* get_constructor(const std::vector<TypeId>& arg_types) {
        for (const auto& ctor : m_constructors) {
            if (ctor->arg_types() == arg_types) {
                return ctor.get();
            }
        }
        return nullptr;
    }

    Cls& set_to_string(ToStringFunc func);

    std::string to_string(Ref ref) const;

    std::vector<Property*> get_properties() const {
        std::vector<Property*> props;
        props.reserve(m_properties.size());
        for (const auto& pair : m_properties) {
            props.push_back(pair.second.get());
        }
        return props;
    }

    TypeId type_id() const { return m_type_id; }
};

} // namespace fei
