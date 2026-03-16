#include "refl/callable.hpp"

#include "refl/val.hpp"

namespace fei {

Param::Param(std::string name, TypeId type_id) :
    m_name(std::move(name)), m_type_id(type_id) {}

TypeId Param::type_id() const {
    return m_type_id;
}

const std::string& Param::name() const {
    return m_name;
}

void Param::set_name(const std::string& name) {
    m_name = name;
}

ReturnValue::ReturnValue() : m_kind(Kind::Void) {}

ReturnValue::ReturnValue(Val val) :
    m_val(std::make_shared<Val>(std::move(val))), m_kind(Kind::Value) {}

ReturnValue::ReturnValue(Ref ref) : m_ref(ref), m_kind(Kind::Reference) {}

ReturnValue::Kind ReturnValue::kind() const {
    return m_kind;
}

Val& ReturnValue::value() {
    assert(m_kind == Kind::Value);
    return *m_val;
}

Ref ReturnValue::ref() const {
    assert(m_kind == Kind::Reference || m_kind == Kind::Value);
    if (m_kind == Kind::Value) {
        return m_val->ref();
    }
    return m_ref;
}

bool ReturnValue::is_value() const {
    return m_kind == Kind::Value;
}

bool ReturnValue::is_ref() const {
    return m_kind == Kind::Reference;
}

bool ReturnValue::is_void() const {
    return m_kind == Kind::Void;
}

Callable::Callable(
    std::string name,
    const std::vector<Param>& params,
    const QualType& return_type
) : m_name(std::move(name)), m_params(params), m_return_type(return_type) {}

bool Callable::validate(const std::vector<Ref>& args) const {
    // TODO: check argument types
    return args.size() == m_params.size();
}

ReturnValue Callable::invoke(Ref arg0) const {
    return invoke_variadic({arg0});
}

ReturnValue Callable::invoke(Ref arg0, Ref arg1) const {
    return invoke_variadic({arg0, arg1});
}

ReturnValue Callable::invoke(Ref arg0, Ref arg1, Ref arg2) const {
    return invoke_variadic({arg0, arg1, arg2});
}

ReturnValue Callable::invoke(Ref arg0, Ref arg1, Ref arg2, Ref arg3) const {
    return invoke_variadic({arg0, arg1, arg2, arg3});
}

ReturnValue
Callable::invoke(Ref arg0, Ref arg1, Ref arg2, Ref arg3, Ref arg4) const {
    return invoke_variadic({arg0, arg1, arg2, arg3, arg4});
}

ReturnValue
Callable::invoke(Ref arg0, Ref arg1, Ref arg2, Ref arg3, Ref arg4, Ref arg5)
    const {
    return invoke_variadic({arg0, arg1, arg2, arg3, arg4, arg5});
}

const std::string& Callable::name() const {
    return m_name;
}

const std::vector<Param>& Callable::params() const {
    return m_params;
}

QualType Callable::return_type() const {
    return m_return_type;
}

} // namespace fei
