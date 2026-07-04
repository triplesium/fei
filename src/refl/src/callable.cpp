#include "refl/callable.hpp"

#include <cassert>

namespace fei {

Param::Param(std::string name, TypeId type_id) :
    m_name(std::move(name)), m_type(type_id) {}

Param::Param(std::string name, QualType type) :
    m_name(std::move(name)), m_type(type) {}

TypeId Param::type_id() const {
    return m_type.type_id();
}

QualType Param::type() const {
    return m_type;
}

const std::string& Param::name() const {
    return m_name;
}

void Param::set_name(const std::string& name) {
    m_name = name;
}

ReturnItem ReturnItem::value(Val value) {
    ReturnItem item;
    item.m_kind = Kind::Value;
    item.m_val = std::move(value);
    return item;
}

ReturnItem ReturnItem::reference(Ref ref) {
    ReturnItem item;
    item.m_kind = Kind::Reference;
    item.m_ref = ref;
    return item;
}

ReturnItem::Kind ReturnItem::kind() const {
    return m_kind;
}

Val& ReturnItem::value() {
    assert(is_value());
    return m_val;
}

const Val& ReturnItem::value() const {
    assert(is_value());
    return m_val;
}

Ref ReturnItem::ref() const {
    assert(is_ref());
    return m_ref;
}

bool ReturnItem::is_value() const {
    return m_kind == Kind::Value;
}

bool ReturnItem::is_ref() const {
    return m_kind == Kind::Reference;
}

ReturnValue::ReturnValue(Val value) :
    ReturnValue(ReturnValue::value(std::move(value))) {}

ReturnValue::ReturnValue(Ref ref) : ReturnValue(ReturnValue::reference(ref)) {}

ReturnValue ReturnValue::void_value() {
    return {};
}

ReturnValue ReturnValue::status() {
    ReturnValue ret;
    ret.m_kind = Kind::Status;
    return ret;
}

ReturnValue ReturnValue::value(Val value) {
    ReturnValue ret;
    ret.m_kind = Kind::One;
    ret.m_item = ReturnItem::value(std::move(value));
    return ret;
}

ReturnValue ReturnValue::reference(Ref ref) {
    ReturnValue ret;
    ret.m_kind = Kind::One;
    ret.m_item = ReturnItem::reference(ref);
    return ret;
}

ReturnValue ReturnValue::many(std::vector<ReturnItem> items) {
    ReturnValue ret;
    ret.m_kind = Kind::Many;
    ret.m_items = std::move(items);
    return ret;
}

ReturnValue::Kind ReturnValue::kind() const {
    return m_kind;
}

const ReturnItem& ReturnValue::item() const {
    assert(is_one());
    return m_item;
}

ReturnItem& ReturnValue::item() {
    assert(is_one());
    return m_item;
}

const std::vector<ReturnItem>& ReturnValue::items() const {
    assert(is_many());
    return m_items;
}

Val& ReturnValue::value() {
    assert(is_value());
    return m_item.value();
}

const Val& ReturnValue::value() const {
    assert(is_value());
    return m_item.value();
}

Ref ReturnValue::ref() const {
    assert(is_ref() || is_value());
    if (is_ref()) {
        return m_item.ref();
    }
    return m_item.value().ref();
}

bool ReturnValue::is_value() const {
    return is_one() && m_item.is_value();
}

bool ReturnValue::is_ref() const {
    return is_one() && m_item.is_ref();
}

bool ReturnValue::is_void() const {
    return m_kind == Kind::Void;
}

bool ReturnValue::is_status() const {
    return m_kind == Kind::Status;
}

bool ReturnValue::is_one() const {
    return m_kind == Kind::One;
}

bool ReturnValue::is_many() const {
    return m_kind == Kind::Many;
}

InvokeFailure InvokeFailure::invalid_call(std::string message) {
    InvokeFailure failure;
    failure.kind = Kind::InvalidCall;
    failure.message = std::move(message);
    return failure;
}

InvokeFailure InvokeFailure::returned_error(Val error) {
    InvokeFailure failure;
    failure.kind = Kind::ReturnedError;
    failure.message = "Reflected function returned an error";
    failure.error = std::move(error);
    return failure;
}

Callable::Callable(
    std::string name,
    const std::vector<Param>& params,
    const QualType& return_type
) : m_name(std::move(name)), m_params(params), m_return_type(return_type) {}

bool Callable::validate(const std::vector<Ref>& args) const {
    if (args.size() != m_params.size()) {
        return false;
    }
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (!args[i] || args[i].type_id() != m_params[i].type_id()) {
            return false;
        }
    }
    return true;
}

InvokeResult Callable::invoke(Ref arg0) const {
    return invoke_variadic({arg0});
}

InvokeResult Callable::invoke(Ref arg0, Ref arg1) const {
    return invoke_variadic({arg0, arg1});
}

InvokeResult Callable::invoke(Ref arg0, Ref arg1, Ref arg2) const {
    return invoke_variadic({arg0, arg1, arg2});
}

InvokeResult Callable::invoke(Ref arg0, Ref arg1, Ref arg2, Ref arg3) const {
    return invoke_variadic({arg0, arg1, arg2, arg3});
}

InvokeResult
Callable::invoke(Ref arg0, Ref arg1, Ref arg2, Ref arg3, Ref arg4) const {
    return invoke_variadic({arg0, arg1, arg2, arg3, arg4});
}

InvokeResult Callable::invoke(
    Ref arg0,
    Ref arg1,
    Ref arg2,
    Ref arg3,
    Ref arg4,
    Ref arg5
) const {
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