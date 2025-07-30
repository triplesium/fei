#pragma once

#include "refl/member.hpp"
#include "refl/method.hpp"
#include "refl/type.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace fei {

class Cls {
  private:
    TypeId m_type_id;
    std::unordered_map<std::string, std::unique_ptr<Property>> m_properties;
    std::unordered_map<std::string, std::unique_ptr<Method>> m_methods;

    using ToStringFunc = std::string (*)(Ref);
    ToStringFunc m_to_string_func = nullptr;

  public:
    Cls(TypeId type_id) : m_type_id(type_id) {}

    template<typename P>
    Cls& add_property(std::string name, P member_ptr) {
        m_properties[name] = std::make_unique<TProperty<P>>(name, member_ptr);
        return *this;
    }

    template<typename P>
    Cls& add_method(std::string name, P method_ptr) {
        m_methods[name] = std::make_unique<TMethod<P>>(name, method_ptr);
        return *this;
    }

    Cls& set_to_string(ToStringFunc func);

    std::string to_string(Ref ref) const;

    const std::unordered_map<std::string, std::unique_ptr<Property>>&
    properties() const {
        return m_properties;
    }

    Property& get_property(const std::string& name) {
        auto it = m_properties.find(name);
        if (it != m_properties.end()) {
            return *it->second;
        }
        throw std::runtime_error("Member not found: " + name);
    }

    Method& get_method(const std::string& name) {
        auto it = m_methods.find(name);
        if (it != m_methods.end()) {
            return *it->second;
        }
        throw std::runtime_error("Method not found: " + name);
    }

    TypeId type_id() const { return m_type_id; }
};

} // namespace fei
