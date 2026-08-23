#include "json_schema.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace ets::runtime_protocol::detail {
namespace {

using Json = nlohmann::json;

std::size_t combine_hash(std::size_t seed, std::size_t value) {
    constexpr std::size_t c_hash_magic = 0x9e3779b9U;
    return seed ^ (value + c_hash_magic + (seed << 6U) + (seed >> 2U));
}

struct JsonHash {
    std::size_t operator()(const Json& value) const {
        std::size_t result = std::hash<int> {}(
            value.is_number() ? -1 : static_cast<int>(value.type())
        );
        if (value.is_number()) {
            return combine_hash(
                result,
                std::hash<double> {}(value.get<double>())
            );
        }
        if (value.is_boolean()) {
            return combine_hash(result, std::hash<bool> {}(value.get<bool>()));
        }
        if (value.is_string()) {
            return combine_hash(
                result,
                std::hash<std::string_view> {}(
                    value.get_ref<const std::string&>()
                )
            );
        }
        if (value.is_array()) {
            for (const auto& child : value) {
                result = combine_hash(result, (*this)(child));
            }
        } else if (value.is_object()) {
            for (const auto& [key, child] : value.items()) {
                result =
                    combine_hash(result, std::hash<std::string_view> {}(key));
                result = combine_hash(result, (*this)(child));
            }
        }
        return result;
    }
};

constexpr std::size_t c_max_schema_depth = 64;
constexpr std::size_t c_max_schema_nodes = std::size_t {32} * 1024;
constexpr std::size_t c_max_instance_nodes = std::size_t {64} * 1024;

bool is_identifier(std::string_view value) {
    if (value.empty() ||
        !(std::isalpha(static_cast<unsigned char>(value.front())) != 0 ||
          value.front() == '_')) {
        return false;
    }
    return std::ranges::all_of(value.substr(1), [](char character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
               character == '_';
    });
}

std::string child_path(std::string_view path, std::string_view key) {
    if (is_identifier(key)) {
        return std::string(path) + "." + std::string(key);
    }
    std::string result(path);
    result += "['";
    for (const char character : key) {
        if (character == '\\' || character == '\'') {
            result += '\\';
        }
        result += character;
    }
    result += "']";
    return result;
}

std::string index_path(std::string_view path, std::size_t index) {
    return std::string(path) + "[" + std::to_string(index) + "]";
}

bool is_schema(const Json& value) {
    return value.is_object() || value.is_boolean();
}

bool is_nonnegative_integer(const Json& value) {
    if (value.is_number_unsigned()) {
        return true;
    }
    return value.is_number_integer() && value.get<std::int64_t>() >= 0;
}

bool is_supported_type(std::string_view type) {
    return type == "null" || type == "boolean" || type == "object" ||
           type == "array" || type == "number" || type == "integer" ||
           type == "string";
}

bool is_supported_keyword(std::string_view keyword) {
    return keyword == "$schema" || keyword == "$id" || keyword == "$ref" ||
           keyword == "$defs" || keyword == "$comment" || keyword == "title" ||
           keyword == "description" || keyword == "default" ||
           keyword == "examples" || keyword == "deprecated" ||
           keyword == "readOnly" || keyword == "writeOnly" ||
           keyword == "type" || keyword == "enum" || keyword == "const" ||
           keyword == "allOf" || keyword == "anyOf" || keyword == "oneOf" ||
           keyword == "not" || keyword == "if" || keyword == "then" ||
           keyword == "else" || keyword == "properties" ||
           keyword == "required" || keyword == "additionalProperties" ||
           keyword == "minProperties" || keyword == "maxProperties" ||
           keyword == "items" || keyword == "prefixItems" ||
           keyword == "minItems" || keyword == "maxItems" ||
           keyword == "uniqueItems" || keyword == "minimum" ||
           keyword == "maximum" || keyword == "exclusiveMinimum" ||
           keyword == "exclusiveMaximum" || keyword == "multipleOf" ||
           keyword == "minLength" || keyword == "maxLength" ||
           keyword == "format";
}

Result<const Json*, std::string>
resolve_reference(const Json& root, std::string_view reference) {
    if (reference.empty() || reference.front() != '#') {
        return failure(
            std::string("only local JSON Schema references are supported")
        );
    }
    const auto pointer = reference.substr(1);
    if (!pointer.empty() && pointer.front() != '/') {
        return failure(
            std::string("local JSON Schema references must use JSON Pointer")
        );
    }
    try {
        const auto& target =
            pointer.empty() ? root :
                              root.at(Json::json_pointer(std::string(pointer)));
        if (!is_schema(target)) {
            return failure(
                std::string("JSON Schema reference target is not a schema")
            );
        }
        return &target;
    } catch (const std::exception& error) {
        return failure(
            std::string("invalid JSON Schema reference: ") + error.what()
        );
    }
}

Status<std::string> validate_schema_node(
    const Json& root,
    const Json& schema,
    std::string_view path,
    std::size_t depth,
    std::size_t& nodes
) {
    if (depth > c_max_schema_depth || ++nodes > c_max_schema_nodes) {
        return failure(
            std::string(path) + " exceeds the schema complexity limit"
        );
    }
    if (schema.is_boolean()) {
        return {};
    }
    if (!schema.is_object()) {
        return failure(
            std::string(path) + " must be a schema object or boolean"
        );
    }

    for (const auto& [keyword, value] : schema.items()) {
        (void)value;
        if (!is_supported_keyword(keyword)) {
            return failure(
                child_path(path, keyword) + " is not supported by the Entisium "
                                            "playtest Schema profile"
            );
        }
    }

    if (auto value = schema.find("$schema"); value != schema.end()) {
        if (!value->is_string() ||
            value->get<std::string_view>() !=
                "https://json-schema.org/draft/2020-12/schema") {
            return failure(
                child_path(path, "$schema") +
                " must select JSON Schema Draft 2020-12"
            );
        }
    }
    for (const char* keyword : {
             "$id",
             "$comment",
             "title",
             "description",
             "format",
         }) {
        if (auto value = schema.find(keyword);
            value != schema.end() && !value->is_string()) {
            return failure(child_path(path, keyword) + " must be a string");
        }
    }
    for (const char* keyword : {"deprecated", "readOnly", "writeOnly"}) {
        if (auto value = schema.find(keyword);
            value != schema.end() && !value->is_boolean()) {
            return failure(child_path(path, keyword) + " must be a boolean");
        }
    }
    if (auto value = schema.find("examples");
        value != schema.end() && !value->is_array()) {
        return failure(child_path(path, "examples") + " must be an array");
    }

    if (auto value = schema.find("$ref"); value != schema.end()) {
        if (!value->is_string()) {
            return failure(child_path(path, "$ref") + " must be a string");
        }
        auto resolved = resolve_reference(root, value->get<std::string_view>());
        if (!resolved) {
            return failure(child_path(path, "$ref") + ": " + resolved.error());
        }
    }

    if (auto value = schema.find("type"); value != schema.end()) {
        if (value->is_string()) {
            if (!is_supported_type(value->get<std::string_view>())) {
                return failure(child_path(path, "type") + " is not recognized");
            }
        } else if (value->is_array() && !value->empty()) {
            std::unordered_set<std::string> types;
            for (std::size_t index = 0; index < value->size(); ++index) {
                const auto& type = value->at(index);
                if (!type.is_string() ||
                    !is_supported_type(type.get<std::string_view>())) {
                    return failure(
                        index_path(child_path(path, "type"), index) +
                        " must be a recognized type"
                    );
                }
                if (!types.emplace(type.get<std::string>()).second) {
                    return failure(
                        child_path(path, "type") + " must contain unique types"
                    );
                }
            }
        } else {
            return failure(
                child_path(path, "type") +
                " must be a string or non-empty array"
            );
        }
    }

    if (auto value = schema.find("enum");
        value != schema.end() && (!value->is_array() || value->empty())) {
        return failure(child_path(path, "enum") + " must be a non-empty array");
    }
    if (auto value = schema.find("enum"); value != schema.end()) {
        std::unordered_set<Json, JsonHash> entries;
        for (std::size_t index = 0; index < value->size(); ++index) {
            if (!entries.emplace(value->at(index)).second) {
                return failure(
                    index_path(child_path(path, "enum"), index) +
                    " must be unique"
                );
            }
        }
    }

    if (auto definitions = schema.find("$defs"); definitions != schema.end()) {
        if (!definitions->is_object()) {
            return failure(child_path(path, "$defs") + " must be an object");
        }
        for (const auto& [name, child] : definitions->items()) {
            auto status = validate_schema_node(
                root,
                child,
                child_path(child_path(path, "$defs"), name),
                depth + 1,
                nodes
            );
            if (!status) {
                return status;
            }
        }
    }

    if (auto properties = schema.find("properties");
        properties != schema.end()) {
        if (!properties->is_object()) {
            return failure(
                child_path(path, "properties") + " must be an object"
            );
        }
        for (const auto& [name, child] : properties->items()) {
            auto status = validate_schema_node(
                root,
                child,
                child_path(child_path(path, "properties"), name),
                depth + 1,
                nodes
            );
            if (!status) {
                return status;
            }
        }
    }

    if (auto required = schema.find("required"); required != schema.end()) {
        if (!required->is_array()) {
            return failure(child_path(path, "required") + " must be an array");
        }
        std::unordered_set<std::string> names;
        for (std::size_t index = 0; index < required->size(); ++index) {
            const auto& name = required->at(index);
            if (!name.is_string()) {
                return failure(
                    index_path(child_path(path, "required"), index) +
                    " must be a string"
                );
            }
            if (!names.emplace(name.get<std::string>()).second) {
                return failure(
                    child_path(path, "required") +
                    " must contain unique property names"
                );
            }
        }
    }

    for (const char* keyword :
         {"additionalProperties", "items", "not", "if", "then", "else"}) {
        if (auto child = schema.find(keyword); child != schema.end()) {
            if (!is_schema(*child)) {
                return failure(
                    child_path(path, keyword) +
                    " must be a schema object or boolean"
                );
            }
            auto status = validate_schema_node(
                root,
                *child,
                child_path(path, keyword),
                depth + 1,
                nodes
            );
            if (!status) {
                return status;
            }
        }
    }

    for (const char* keyword : {"prefixItems", "allOf", "anyOf", "oneOf"}) {
        if (auto children = schema.find(keyword); children != schema.end()) {
            if (!children->is_array() ||
                (std::string_view(keyword) != "prefixItems" &&
                 children->empty())) {
                return failure(
                    child_path(path, keyword) + " must be an array" +
                    (std::string_view(keyword) == "prefixItems" ?
                         "" :
                         " with entries")
                );
            }
            for (std::size_t index = 0; index < children->size(); ++index) {
                auto status = validate_schema_node(
                    root,
                    children->at(index),
                    index_path(child_path(path, keyword), index),
                    depth + 1,
                    nodes
                );
                if (!status) {
                    return status;
                }
            }
        }
    }

    for (const char* keyword : {
             "minProperties",
             "maxProperties",
             "minItems",
             "maxItems",
             "minLength",
             "maxLength",
         }) {
        if (auto value = schema.find(keyword);
            value != schema.end() && !is_nonnegative_integer(*value)) {
            return failure(
                child_path(path, keyword) + " must be a non-negative integer"
            );
        }
    }
    for (const char* keyword : {
             "minimum",
             "maximum",
             "exclusiveMinimum",
             "exclusiveMaximum",
         }) {
        if (auto value = schema.find(keyword);
            value != schema.end() && !value->is_number()) {
            return failure(child_path(path, keyword) + " must be a number");
        }
    }
    if (auto value = schema.find("multipleOf"); value != schema.end()) {
        if (!value->is_number() || value->get<double>() <= 0.0) {
            return failure(
                child_path(path, "multipleOf") + " must be a positive number"
            );
        }
    }
    if (auto value = schema.find("uniqueItems");
        value != schema.end() && !value->is_boolean()) {
        return failure(child_path(path, "uniqueItems") + " must be a boolean");
    }

    for (const auto& [minimum, maximum] : {
             std::pair<const char*, const char*> {
                 "minProperties",
                 "maxProperties",
             },
             {"minItems", "maxItems"},
             {"minLength", "maxLength"},
         }) {
        if (schema.contains(minimum) && schema.contains(maximum) &&
            schema.at(minimum).get<std::uint64_t>() >
                schema.at(maximum).get<std::uint64_t>()) {
            return failure(
                child_path(path, minimum) + " must not exceed " + maximum
            );
        }
    }
    return {};
}

bool matches_type(const Json& instance, std::string_view type) {
    if (type == "null") {
        return instance.is_null();
    }
    if (type == "boolean") {
        return instance.is_boolean();
    }
    if (type == "object") {
        return instance.is_object();
    }
    if (type == "array") {
        return instance.is_array();
    }
    if (type == "number") {
        return instance.is_number();
    }
    if (type == "integer") {
        if (instance.is_number_integer() || instance.is_number_unsigned()) {
            return true;
        }
        return instance.is_number_float() &&
               std::trunc(instance.get<double>()) == instance.get<double>();
    }
    return type == "string" && instance.is_string();
}

std::string instance_type_name(const Json& instance) {
    if (instance.is_number_integer() || instance.is_number_unsigned()) {
        return "integer";
    }
    if (instance.is_number_float()) {
        return "number";
    }
    return instance.type_name();
}

std::size_t utf8_length(std::string_view value) {
    return static_cast<std::size_t>(
        std::ranges::count_if(value, [](unsigned char byte) {
            return (byte & 0xc0U) != 0x80U;
        })
    );
}

class InstanceValidator {
  public:
    explicit InstanceValidator(const Json& root) : m_root(root) {}

    std::optional<std::string> validate(
        const Json& schema,
        const Json& instance,
        std::string path = "$",
        std::size_t depth = 0
    ) {
        if (depth > c_max_schema_depth || ++m_nodes > c_max_instance_nodes) {
            return path + " exceeds the validation complexity limit";
        }
        if (schema.is_boolean()) {
            if (schema.get<bool>()) {
                return std::nullopt;
            }
            return path + " is rejected by schema";
        }

        if (auto reference = schema.find("$ref"); reference != schema.end()) {
            auto resolved =
                resolve_reference(m_root, reference->get<std::string_view>());
            if (!resolved) {
                return path + ": " + resolved.error();
            }
            if (auto error = validate(**resolved, instance, path, depth + 1)) {
                return error;
            }
        }

        if (auto type = schema.find("type"); type != schema.end()) {
            bool matches = false;
            std::string expected;
            if (type->is_string()) {
                expected = type->get<std::string>();
                matches = matches_type(instance, expected);
            } else {
                for (const auto& candidate : *type) {
                    if (!expected.empty()) {
                        expected += ", ";
                    }
                    expected += candidate.get<std::string>();
                    matches = matches || matches_type(
                                             instance,
                                             candidate.get<std::string_view>()
                                         );
                }
            }
            if (!matches) {
                return path + " must be " + expected + ", got " +
                       instance_type_name(instance);
            }
        }

        if (auto value = schema.find("const");
            value != schema.end() && instance != *value) {
            return path + " must equal " + value->dump();
        }
        if (auto values = schema.find("enum");
            values != schema.end() &&
            std::ranges::find(*values, instance) == values->end()) {
            return path + " must be one of " + values->dump();
        }

        if (auto error = validate_combinators(schema, instance, path, depth)) {
            return error;
        }
        if (instance.is_object()) {
            if (auto error = validate_object(schema, instance, path, depth)) {
                return error;
            }
        }
        if (instance.is_array()) {
            if (auto error = validate_array(schema, instance, path, depth)) {
                return error;
            }
        }
        if (instance.is_string()) {
            if (auto error = validate_string(schema, instance, path)) {
                return error;
            }
        }
        if (instance.is_number()) {
            if (auto error = validate_number(schema, instance, path)) {
                return error;
            }
        }
        return std::nullopt;
    }

  private:
    std::optional<std::string> validate_combinators(
        const Json& schema,
        const Json& instance,
        const std::string& path,
        std::size_t depth
    ) {
        if (auto children = schema.find("allOf"); children != schema.end()) {
            for (const auto& child : *children) {
                if (auto error = validate(child, instance, path, depth + 1)) {
                    return error;
                }
            }
        }
        if (auto children = schema.find("anyOf"); children != schema.end()) {
            bool matched = false;
            for (const auto& child : *children) {
                if (!validate(child, instance, path, depth + 1)) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                return path + " must match at least one anyOf schema";
            }
        }
        if (auto children = schema.find("oneOf"); children != schema.end()) {
            std::size_t matches = 0;
            for (const auto& child : *children) {
                if (!validate(child, instance, path, depth + 1)) {
                    ++matches;
                }
            }
            if (matches != 1) {
                return path + " must match exactly one oneOf schema";
            }
        }
        if (auto child = schema.find("not");
            child != schema.end() &&
            !validate(*child, instance, path, depth + 1)) {
            return path + " must not match the not schema";
        }
        if (auto condition = schema.find("if"); condition != schema.end()) {
            const bool matched =
                !validate(*condition, instance, path, depth + 1);
            const char* branch = matched ? "then" : "else";
            if (auto child = schema.find(branch); child != schema.end()) {
                return validate(*child, instance, path, depth + 1);
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> validate_object(
        const Json& schema,
        const Json& instance,
        const std::string& path,
        std::size_t depth
    ) {
        if (auto minimum = schema.find("minProperties");
            minimum != schema.end() &&
            instance.size() < minimum->get<std::size_t>()) {
            return path + " must contain at least " + minimum->dump() +
                   " properties";
        }
        if (auto maximum = schema.find("maxProperties");
            maximum != schema.end() &&
            instance.size() > maximum->get<std::size_t>()) {
            return path + " must contain at most " + maximum->dump() +
                   " properties";
        }
        if (auto required = schema.find("required"); required != schema.end()) {
            for (const auto& name : *required) {
                const auto key = name.get<std::string>();
                if (!instance.contains(key)) {
                    return child_path(path, key) + " is required";
                }
            }
        }

        const auto properties = schema.find("properties");
        const auto additional = schema.find("additionalProperties");
        for (const auto& [name, value] : instance.items()) {
            if (properties != schema.end() && properties->contains(name)) {
                if (auto error = validate(
                        properties->at(name),
                        value,
                        child_path(path, name),
                        depth + 1
                    )) {
                    return error;
                }
                continue;
            }
            if (additional == schema.end() ||
                (additional->is_boolean() && additional->get<bool>())) {
                continue;
            }
            if (additional->is_boolean()) {
                return child_path(path, name) + " is not allowed";
            }
            if (auto error = validate(
                    *additional,
                    value,
                    child_path(path, name),
                    depth + 1
                )) {
                return error;
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> validate_array(
        const Json& schema,
        const Json& instance,
        const std::string& path,
        std::size_t depth
    ) {
        if (auto minimum = schema.find("minItems");
            minimum != schema.end() &&
            instance.size() < minimum->get<std::size_t>()) {
            return path + " must contain at least " + minimum->dump() +
                   " items";
        }
        if (auto maximum = schema.find("maxItems");
            maximum != schema.end() &&
            instance.size() > maximum->get<std::size_t>()) {
            return path + " must contain at most " + maximum->dump() + " items";
        }
        if (schema.value("uniqueItems", false)) {
            std::unordered_set<Json, JsonHash> values;
            for (std::size_t index = 0; index < instance.size(); ++index) {
                if (!values.emplace(instance.at(index)).second) {
                    return index_path(path, index) + " must be unique";
                }
            }
        }

        std::size_t prefix_size = 0;
        if (auto prefix = schema.find("prefixItems"); prefix != schema.end()) {
            prefix_size = std::min(prefix->size(), instance.size());
            for (std::size_t index = 0; index < prefix_size; ++index) {
                if (auto error = validate(
                        prefix->at(index),
                        instance.at(index),
                        index_path(path, index),
                        depth + 1
                    )) {
                    return error;
                }
            }
        }
        if (auto items = schema.find("items"); items != schema.end()) {
            for (std::size_t index = prefix_size; index < instance.size();
                 ++index) {
                if (auto error = validate(
                        *items,
                        instance.at(index),
                        index_path(path, index),
                        depth + 1
                    )) {
                    return error;
                }
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> validate_string(
        const Json& schema,
        const Json& instance,
        const std::string& path
    ) const {
        const auto length = utf8_length(instance.get_ref<const std::string&>());
        if (auto minimum = schema.find("minLength");
            minimum != schema.end() && length < minimum->get<std::size_t>()) {
            return path + " must contain at least " + minimum->dump() +
                   " characters";
        }
        if (auto maximum = schema.find("maxLength");
            maximum != schema.end() && length > maximum->get<std::size_t>()) {
            return path + " must contain at most " + maximum->dump() +
                   " characters";
        }
        return std::nullopt;
    }

    std::optional<std::string> validate_number(
        const Json& schema,
        const Json& instance,
        const std::string& path
    ) const {
        const auto number = instance.get<double>();
        if (auto value = schema.find("minimum");
            value != schema.end() && number < value->get<double>()) {
            return path + " must be at least " + value->dump();
        }
        if (auto value = schema.find("maximum");
            value != schema.end() && number > value->get<double>()) {
            return path + " must be at most " + value->dump();
        }
        if (auto value = schema.find("exclusiveMinimum");
            value != schema.end() && number <= value->get<double>()) {
            return path + " must be greater than " + value->dump();
        }
        if (auto value = schema.find("exclusiveMaximum");
            value != schema.end() && number >= value->get<double>()) {
            return path + " must be less than " + value->dump();
        }
        if (auto value = schema.find("multipleOf"); value != schema.end()) {
            const auto divisor = value->get<double>();
            const auto quotient = number / divisor;
            const auto tolerance = std::numeric_limits<double>::epsilon() *
                                   std::max(1.0, std::abs(quotient)) * 8.0;
            if (std::abs(quotient - std::round(quotient)) > tolerance) {
                return path + " must be a multiple of " + value->dump();
            }
        }
        return std::nullopt;
    }

    const Json& m_root;
    std::size_t m_nodes {0};
};

} // namespace

Result<Json, std::string> compile_json_schema(std::string_view source) {
    Json schema;
    try {
        schema = Json::parse(source);
    } catch (const std::exception& error) {
        return failure(std::string("is not valid JSON: ") + error.what());
    }
    if (!schema.is_object()) {
        return failure(std::string("must be a JSON object"));
    }
    std::size_t nodes = 0;
    if (auto status = validate_schema_node(schema, schema, "$", 0, nodes);
        !status) {
        return failure(std::move(status.error()));
    }
    return schema;
}

std::optional<std::string>
validate_json_schema_instance(const Json& schema, const Json& instance) {
    return InstanceValidator(schema).validate(schema, instance);
}

} // namespace ets::runtime_protocol::detail
