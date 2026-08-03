#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace fei::serialization {

struct SerializedField;

class SerializedNode {
  public:
    using Array = std::vector<SerializedNode>;
    using Object = std::vector<SerializedField>;
    using Value = std::variant<
        std::nullptr_t,
        bool,
        std::int64_t,
        std::uint64_t,
        double,
        std::string,
        Array,
        Object>;

    enum class Kind {
        Null,
        Bool,
        SignedInteger,
        UnsignedInteger,
        Floating,
        String,
        Array,
        Object,
    };

  private:
    Value m_value;

  public:
    SerializedNode();
    SerializedNode(std::nullptr_t);
    explicit SerializedNode(bool value);
    explicit SerializedNode(std::int64_t value);
    explicit SerializedNode(std::uint64_t value);
    explicit SerializedNode(double value);
    explicit SerializedNode(std::string value);

    static SerializedNode null();
    static SerializedNode boolean(bool value);
    static SerializedNode signed_integer(std::int64_t value);
    static SerializedNode unsigned_integer(std::uint64_t value);
    static SerializedNode floating(double value);
    static SerializedNode string(std::string value);
    static SerializedNode array(Array value);
    static SerializedNode object(Object value);

    Kind kind() const;
    const Value& value() const { return m_value; }
    Value& value() { return m_value; }

    bool is_null() const;
    bool is_bool() const;
    bool is_signed_integer() const;
    bool is_unsigned_integer() const;
    bool is_floating() const;
    bool is_number() const;
    bool is_string() const;
    bool is_array() const;
    bool is_object() const;

    const bool* try_bool() const;
    const std::int64_t* try_signed_integer() const;
    const std::uint64_t* try_unsigned_integer() const;
    const double* try_floating() const;
    const std::string* try_string() const;
    const Array* try_array() const;
    const Object* try_object() const;

    Array* try_array();
    Object* try_object();
};

struct SerializedField {
    std::string name;
    SerializedNode value;
};

const SerializedField*
find_field(const SerializedNode::Object& object, const std::string& name);

SerializedField*
find_field(SerializedNode::Object& object, const std::string& name);

} // namespace fei::serialization
