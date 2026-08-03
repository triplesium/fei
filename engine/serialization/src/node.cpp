#include "serialization/node.hpp"

#include <algorithm>
#include <utility>

namespace fei::serialization {

SerializedNode::SerializedNode() : m_value(nullptr) {}

SerializedNode::SerializedNode(std::nullptr_t) : m_value(nullptr) {}

SerializedNode::SerializedNode(bool value) : m_value(value) {}

SerializedNode::SerializedNode(std::int64_t value) : m_value(value) {}

SerializedNode::SerializedNode(std::uint64_t value) : m_value(value) {}

SerializedNode::SerializedNode(double value) : m_value(value) {}

SerializedNode::SerializedNode(std::string value) : m_value(std::move(value)) {}

SerializedNode SerializedNode::null() {
    return SerializedNode(nullptr);
}

SerializedNode SerializedNode::boolean(bool value) {
    return SerializedNode(value);
}

SerializedNode SerializedNode::signed_integer(std::int64_t value) {
    return SerializedNode(value);
}

SerializedNode SerializedNode::unsigned_integer(std::uint64_t value) {
    return SerializedNode(value);
}

SerializedNode SerializedNode::floating(double value) {
    return SerializedNode(value);
}

SerializedNode SerializedNode::string(std::string value) {
    return SerializedNode(std::move(value));
}

SerializedNode SerializedNode::array(Array value) {
    SerializedNode node;
    node.m_value = std::move(value);
    return node;
}

SerializedNode SerializedNode::object(Object value) {
    SerializedNode node;
    node.m_value = std::move(value);
    return node;
}

SerializedNode::Kind SerializedNode::kind() const {
    switch (m_value.index()) {
        case 0:
            return Kind::Null;
        case 1:
            return Kind::Bool;
        case 2:
            return Kind::SignedInteger;
        case 3:
            return Kind::UnsignedInteger;
        case 4:
            return Kind::Floating;
        case 5:
            return Kind::String;
        case 6:
            return Kind::Array;
        case 7:
            return Kind::Object;
        default:
            return Kind::Null;
    }
}

bool SerializedNode::is_null() const {
    return std::holds_alternative<std::nullptr_t>(m_value);
}

bool SerializedNode::is_bool() const {
    return std::holds_alternative<bool>(m_value);
}

bool SerializedNode::is_signed_integer() const {
    return std::holds_alternative<std::int64_t>(m_value);
}

bool SerializedNode::is_unsigned_integer() const {
    return std::holds_alternative<std::uint64_t>(m_value);
}

bool SerializedNode::is_floating() const {
    return std::holds_alternative<double>(m_value);
}

bool SerializedNode::is_number() const {
    return is_signed_integer() || is_unsigned_integer() || is_floating();
}

bool SerializedNode::is_string() const {
    return std::holds_alternative<std::string>(m_value);
}

bool SerializedNode::is_array() const {
    return std::holds_alternative<Array>(m_value);
}

bool SerializedNode::is_object() const {
    return std::holds_alternative<Object>(m_value);
}

const bool* SerializedNode::try_bool() const {
    return std::get_if<bool>(&m_value);
}

const std::int64_t* SerializedNode::try_signed_integer() const {
    return std::get_if<std::int64_t>(&m_value);
}

const std::uint64_t* SerializedNode::try_unsigned_integer() const {
    return std::get_if<std::uint64_t>(&m_value);
}

const double* SerializedNode::try_floating() const {
    return std::get_if<double>(&m_value);
}

const std::string* SerializedNode::try_string() const {
    return std::get_if<std::string>(&m_value);
}

const SerializedNode::Array* SerializedNode::try_array() const {
    return std::get_if<Array>(&m_value);
}

const SerializedNode::Object* SerializedNode::try_object() const {
    return std::get_if<Object>(&m_value);
}

SerializedNode::Array* SerializedNode::try_array() {
    return std::get_if<Array>(&m_value);
}

SerializedNode::Object* SerializedNode::try_object() {
    return std::get_if<Object>(&m_value);
}

const SerializedField*
find_field(const SerializedNode::Object& object, const std::string& name) {
    auto it = std::ranges::find(object, name, &SerializedField::name);
    return it == object.end() ? nullptr : &*it;
}

SerializedField*
find_field(SerializedNode::Object& object, const std::string& name) {
    auto it = std::ranges::find(object, name, &SerializedField::name);
    return it == object.end() ? nullptr : &*it;
}

} // namespace fei::serialization
