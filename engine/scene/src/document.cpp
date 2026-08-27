#include "scene/document.hpp"

#include "ecs/annotations.hpp"
#include "ecs/world.hpp"
#include "refl/registry.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <yaml-cpp/yaml.h> // IWYU pragma: keep

namespace ets {

namespace {

using serialization::SerializedField;
using serialization::SerializedNode;

SceneDocumentError scene_error(
    SceneDocumentErrorKind kind,
    std::string path,
    std::string message
) {
    return SceneDocumentError {
        .kind = kind,
        .path = std::move(path),
        .message = std::move(message),
    };
}

Result<SerializedNode, SceneDocumentError>
decode_yaml_node(const YAML::Node& node, const std::string& path) {
    if (!node || node.IsNull()) {
        return SerializedNode::null();
    }
    if (node.IsSequence()) {
        SerializedNode::Array values;
        values.reserve(node.size());
        for (std::size_t index = 0; index < node.size(); ++index) {
            auto value = decode_yaml_node(
                node[index],
                path + "[" + std::to_string(index) + "]"
            );
            if (!value) {
                return failure(std::move(value.error()));
            }
            values.push_back(std::move(*value));
        }
        return SerializedNode::array(std::move(values));
    }
    if (node.IsMap()) {
        SerializedNode::Object fields;
        std::unordered_set<std::string> names;
        fields.reserve(node.size());
        for (const auto& entry : node) {
            if (!entry.first.IsScalar()) {
                return failure(scene_error(
                    SceneDocumentErrorKind::InvalidDocument,
                    path,
                    "YAML mapping keys must be strings"
                ));
            }
            auto name = entry.first.as<std::string>();
            auto child_path = path + ".";
            child_path += name;
            if (!names.emplace(name).second) {
                return failure(scene_error(
                    SceneDocumentErrorKind::InvalidDocument,
                    child_path,
                    "Duplicate YAML mapping key"
                ));
            }
            auto value = decode_yaml_node(entry.second, child_path);
            if (!value) {
                return failure(std::move(value.error()));
            }
            fields.push_back(
                SerializedField {
                    .name = std::move(name),
                    .value = std::move(*value),
                }
            );
        }
        return SerializedNode::object(std::move(fields));
    }
    if (!node.IsScalar()) {
        return failure(scene_error(
            SceneDocumentErrorKind::InvalidDocument,
            path,
            "Unsupported YAML node"
        ));
    }

    const auto& scalar = node.Scalar();
    if (node.Tag() == "!") {
        return SerializedNode::string(scalar);
    }
    if (scalar == "true") {
        return SerializedNode::boolean(true);
    }
    if (scalar == "false") {
        return SerializedNode::boolean(false);
    }

    if (!scalar.empty() && scalar.front() == '-') {
        std::int64_t value = 0;
        const auto result = std::from_chars(
            scalar.data(),
            scalar.data() + scalar.size(),
            value
        );
        if (result.ec == std::errc {} &&
            result.ptr == scalar.data() + scalar.size()) {
            return SerializedNode::signed_integer(value);
        }
    } else if (!scalar.empty()) {
        std::uint64_t value = 0;
        const auto result = std::from_chars(
            scalar.data(),
            scalar.data() + scalar.size(),
            value
        );
        if (result.ec == std::errc {} &&
            result.ptr == scalar.data() + scalar.size()) {
            return SerializedNode::unsigned_integer(value);
        }
    }

    if (scalar.find_first_of(".eE") != std::string::npos) {
        double value = 0.0;
        const auto result = std::from_chars(
            scalar.data(),
            scalar.data() + scalar.size(),
            value
        );
        if (result.ec == std::errc {} &&
            result.ptr == scalar.data() + scalar.size() &&
            std::isfinite(value)) {
            return SerializedNode::floating(value);
        }
    }
    return SerializedNode::string(scalar);
}

void emit_yaml_node(YAML::Emitter& output, const SerializedNode& node) {
    switch (node.kind()) { // NOLINT(bugprone-branch-clone)
        case SerializedNode::Kind::Null:
            output << YAML::Null;
            return;
        case SerializedNode::Kind::Bool:
            output << *node.try_bool();
            return;
        case SerializedNode::Kind::SignedInteger:
            output << *node.try_signed_integer();
            return;
        case SerializedNode::Kind::UnsignedInteger:
            output << *node.try_unsigned_integer();
            return;
        case SerializedNode::Kind::Floating:
            output << *node.try_floating();
            return;
        case SerializedNode::Kind::String:
            output << YAML::DoubleQuoted << *node.try_string();
            return;
        case SerializedNode::Kind::Array:
            output << YAML::BeginSeq;
            for (const auto& value : *node.try_array()) {
                emit_yaml_node(output, value);
            }
            output << YAML::EndSeq;
            return;
        case SerializedNode::Kind::Object:
            output << YAML::BeginMap;
            for (const auto& field : *node.try_object()) {
                output << YAML::Key << field.name << YAML::Value;
                emit_yaml_node(output, field.value);
            }
            output << YAML::EndMap;
            return;
    }
}

Status<SceneDocumentError> validate_document(const SceneDocument& document) {
    if (document.version != scene_document_version) {
        return failure(scene_error(
            SceneDocumentErrorKind::InvalidDocument,
            "$.version",
            "Unsupported scene document version " +
                std::to_string(document.version)
        ));
    }

    std::unordered_set<AssetUuid> entity_ids;
    for (std::size_t index = 0; index < document.entities.size(); ++index) {
        const auto& entity = document.entities[index];
        const auto path = "$.entities[" + std::to_string(index) + "]";
        if (entity.id.is_nil() || !entity_ids.emplace(entity.id).second) {
            return failure(scene_error(
                SceneDocumentErrorKind::InvalidDocument,
                path + ".id",
                entity.id.is_nil() ? "Entity ID cannot be nil" :
                                     "Duplicate entity ID"
            ));
        }
        std::unordered_set<std::string> component_types;
        for (const auto& component : entity.components) {
            if (component.type.empty() ||
                !component_types.emplace(component.type).second) {
                return failure(scene_error(
                    SceneDocumentErrorKind::InvalidDocument,
                    path + ".components",
                    component.type.empty() ?
                        "Component type cannot be empty" :
                        "Duplicate component type '" + component.type + "'"
                ));
            }
        }
    }
    for (std::size_t index = 0; index < document.entities.size(); ++index) {
        const auto& entity = document.entities[index];
        if (entity.parent && !entity_ids.contains(*entity.parent)) {
            return failure(scene_error(
                SceneDocumentErrorKind::InvalidDocument,
                "$.entities[" + std::to_string(index) + "].parent",
                "Parent entity does not exist"
            ));
        }
        if (entity.parent && *entity.parent == entity.id) {
            return failure(scene_error(
                SceneDocumentErrorKind::InvalidDocument,
                "$.entities[" + std::to_string(index) + "].parent",
                "Entity cannot be its own parent"
            ));
        }
    }
    std::unordered_map<AssetUuid, Optional<AssetUuid>> parents;
    for (const auto& entity : document.entities) {
        parents.emplace(entity.id, entity.parent);
    }
    for (std::size_t index = 0; index < document.entities.size(); ++index) {
        std::unordered_set<AssetUuid> visited;
        auto current = document.entities[index].id;
        while (true) {
            if (!visited.emplace(current).second) {
                return failure(scene_error(
                    SceneDocumentErrorKind::InvalidDocument,
                    "$.entities[" + std::to_string(index) + "].parent",
                    "Entity hierarchy contains a cycle"
                ));
            }
            const auto parent = parents.find(current);
            if (parent == parents.end() || !parent->second) {
                break;
            }
            current = *parent->second;
        }
    }
    return {};
}

void preserve_unknown_fields(
    SerializedNode& current,
    const SerializedNode& previous
) {
    auto* current_object = current.try_object();
    const auto* previous_object = previous.try_object();
    if (!current_object || !previous_object) {
        return;
    }
    for (const auto& old_field : *previous_object) {
        auto* new_field =
            serialization::find_field(*current_object, old_field.name);
        if (!new_field) {
            current_object->push_back(old_field);
            continue;
        }
        preserve_unknown_fields(new_field->value, old_field.value);
    }
}

} // namespace

void SceneEntityBindings::clear() {
    m_by_entity.clear();
    m_by_id.clear();
}

AssetUuid SceneEntityBindings::ensure(Entity entity) {
    if (const auto found = m_by_entity.find(entity);
        found != m_by_entity.end()) {
        return found->second;
    }
    auto id = AssetUuid::random();
    bind(id, entity);
    return id;
}

void SceneEntityBindings::bind(AssetUuid id, Entity entity) {
    if (const auto old = m_by_entity.find(entity); old != m_by_entity.end()) {
        m_by_id.erase(old->second);
    }
    if (const auto old = m_by_id.find(id); old != m_by_id.end()) {
        m_by_entity.erase(old->second);
    }
    m_by_entity.insert_or_assign(entity, id);
    m_by_id.insert_or_assign(id, entity);
}

Optional<AssetUuid> SceneEntityBindings::id(Entity entity) const {
    const auto found = m_by_entity.find(entity);
    return found == m_by_entity.end() ? Optional<AssetUuid> {} : found->second;
}

Optional<Entity> SceneEntityBindings::entity(AssetUuid id) const {
    const auto found = m_by_id.find(id);
    return found == m_by_id.end() ? Optional<Entity> {} : found->second;
}

std::vector<Entity> SceneEntityBindings::entities() const {
    std::vector<Entity> result;
    result.reserve(m_by_entity.size());
    for (const auto& [entity, id] : m_by_entity) {
        (void)id;
        result.push_back(entity);
    }
    return result;
}

Result<SceneDocument, SceneDocumentError>
parse_scene_document(std::string_view source) {
    YAML::Node root;
    try {
        root = YAML::Load(std::string(source));
    } catch (const YAML::Exception& error) {
        return failure(
            scene_error(SceneDocumentErrorKind::InvalidYaml, "$", error.what())
        );
    }
    if (!root.IsMap()) {
        return failure(scene_error(
            SceneDocumentErrorKind::InvalidDocument,
            "$",
            "Scene document must be a YAML mapping"
        ));
    }

    try {
        const auto format = root["format"];
        const auto version = root["version"];
        const auto entities = root["entities"];
        if (!format.IsScalar() ||
            format.as<std::string>() != scene_document_format) {
            return failure(scene_error(
                SceneDocumentErrorKind::InvalidDocument,
                "$.format",
                "Scene format must be 'entisium.scene'"
            ));
        }
        if (!version.IsScalar() || !entities.IsSequence()) {
            return failure(scene_error(
                SceneDocumentErrorKind::InvalidDocument,
                "$",
                "Scene requires numeric 'version' and sequence 'entities'"
            ));
        }

        SceneDocument document {.version = version.as<std::uint32_t>()};
        document.entities.reserve(entities.size());
        for (std::size_t index = 0; index < entities.size(); ++index) {
            const auto node = entities[index];
            const auto path = "$.entities[" + std::to_string(index) + "]";
            if (!node.IsMap() || !node["id"].IsScalar() ||
                !node["components"].IsMap()) {
                return failure(scene_error(
                    SceneDocumentErrorKind::InvalidDocument,
                    path,
                    "Entity requires string 'id' and mapping 'components'"
                ));
            }
            auto id = AssetUuid::parse(node["id"].as<std::string>());
            if (!id) {
                return failure(scene_error(
                    SceneDocumentErrorKind::InvalidDocument,
                    path + ".id",
                    std::move(id.error())
                ));
            }
            SceneEntityDocument entity {.id = *id};
            if (const auto parent = node["parent"];
                parent && !parent.IsNull()) {
                if (!parent.IsScalar()) {
                    return failure(scene_error(
                        SceneDocumentErrorKind::InvalidDocument,
                        path + ".parent",
                        "Parent must be an entity UUID or null"
                    ));
                }
                auto parent_id = AssetUuid::parse(parent.as<std::string>());
                if (!parent_id) {
                    return failure(scene_error(
                        SceneDocumentErrorKind::InvalidDocument,
                        path + ".parent",
                        std::move(parent_id.error())
                    ));
                }
                entity.parent = *parent_id;
            }
            for (const auto& component_node : node["components"]) {
                if (!component_node.first.IsScalar()) {
                    return failure(scene_error(
                        SceneDocumentErrorKind::InvalidDocument,
                        path + ".components",
                        "Component names must be strings"
                    ));
                }
                auto type = component_node.first.as<std::string>();
                auto component_path = path + ".components.";
                component_path += type;
                auto properties =
                    decode_yaml_node(component_node.second, component_path);
                if (!properties) {
                    return failure(std::move(properties.error()));
                }
                entity.components.push_back(
                    SceneComponentDocument {
                        .type = std::move(type),
                        .properties = std::move(*properties),
                    }
                );
            }
            document.entities.push_back(std::move(entity));
        }
        if (auto status = validate_document(document); !status) {
            return failure(std::move(status.error()));
        }
        return document;
    } catch (const YAML::Exception& error) {
        return failure(scene_error(
            SceneDocumentErrorKind::InvalidDocument,
            "$",
            error.what()
        ));
    }
}

Result<std::string, SceneDocumentError>
write_scene_document(const SceneDocument& document) {
    if (auto status = validate_document(document); !status) {
        return failure(std::move(status.error()));
    }
    YAML::Emitter output;
    output.SetIndent(2);
    output << YAML::BeginMap << YAML::Key << "format" << YAML::Value
           << std::string(scene_document_format) << YAML::Key << "version"
           << YAML::Value << document.version << YAML::Key << "entities"
           << YAML::Value << YAML::BeginSeq;
    for (const auto& entity : document.entities) {
        output << YAML::BeginMap << YAML::Key << "id" << YAML::Value
               << YAML::DoubleQuoted << entity.id.as_string();
        if (entity.parent) {
            output << YAML::Key << "parent" << YAML::Value << YAML::DoubleQuoted
                   << entity.parent->as_string();
        }
        output << YAML::Key << "components" << YAML::Value << YAML::BeginMap;
        for (const auto& component : entity.components) {
            output << YAML::Key << component.type << YAML::Value;
            emit_yaml_node(output, component.properties);
        }
        output << YAML::EndMap << YAML::EndMap;
    }
    output << YAML::EndSeq << YAML::EndMap;
    if (!output.good()) {
        return failure(scene_error(
            SceneDocumentErrorKind::InvalidDocument,
            "$",
            output.GetLastError()
        ));
    }
    return std::string(output.c_str()) + '\n';
}

Result<SceneDocument, SceneDocumentError> capture_scene_document(
    const World& world,
    SceneEntityBindings& bindings,
    const serialization::ValueCodecRegistry* codecs,
    const SceneDocument* preserve_unknown_from
) {
    SceneDocument document;
    std::unordered_map<AssetUuid, const SceneEntityDocument*> preserved;
    if (preserve_unknown_from) {
        for (const auto& entity : preserve_unknown_from->entities) {
            preserved.emplace(entity.id, &entity);
        }
    }

    auto entities = bindings.entities();
    std::erase_if(entities, [&world](Entity entity) {
        return !world.has_entity(entity);
    });
    std::ranges::sort(entities);
    document.entities.reserve(entities.size());
    for (const auto entity : entities) {
        SceneEntityDocument saved {.id = bindings.ensure(entity)};
        if (auto parent = world.parent(entity)) {
            saved.parent = bindings.ensure(*parent);
        }
        const auto location = world.entity_location(entity);
        const auto& component_types =
            world.archetypes().get(location->archetype_id).components();
        for (const auto type_id : component_types) {
            auto type = Registry::instance().try_get_type(type_id);
            if (!type || !type->has_annotation<annotations::Component>()) {
                continue;
            }
            auto properties = serialization::serialize(
                world.get_component(entity, type_id),
                serialization::SerializeOptions {
                    .include_type_tag = false,
                    .codecs = codecs,
                }
            );
            if (!properties) {
                return failure(scene_error(
                    SceneDocumentErrorKind::SerializeComponent,
                    "$.entities[" + saved.id.as_string() + "].components." +
                        type->stripped_name(),
                    properties.error().message
                ));
            }
            if (const auto old = preserved.find(saved.id);
                old != preserved.end()) {
                const auto old_component = std::ranges::find(
                    old->second->components,
                    type->stripped_name(),
                    &SceneComponentDocument::type
                );
                if (old_component != old->second->components.end()) {
                    preserve_unknown_fields(
                        *properties,
                        old_component->properties
                    );
                }
            }
            saved.components.push_back(
                SceneComponentDocument {
                    .type = type->stripped_name(),
                    .properties = std::move(*properties),
                }
            );
        }
        if (const auto old = preserved.find(saved.id); old != preserved.end()) {
            for (const auto& component : old->second->components) {
                if (!Registry::instance().try_get_type(component.type)) {
                    saved.components.push_back(component);
                }
            }
        }
        std::ranges::sort(saved.components, {}, &SceneComponentDocument::type);
        document.entities.push_back(std::move(saved));
    }
    return document;
}

Result<SceneInstantiationResult, SceneDocumentError> instantiate_scene_document(
    const SceneDocument& document,
    World& world,
    const serialization::ValueCodecRegistry* codecs
) {
    if (auto status = validate_document(document); !status) {
        return failure(std::move(status.error()));
    }

    struct PreparedEntity {
        AssetUuid id;
        Optional<AssetUuid> parent;
        std::vector<Val> components;
    };
    std::vector<PreparedEntity> prepared;
    std::vector<std::string> warnings;
    prepared.reserve(document.entities.size());
    std::size_t entity_index = 0;
    for (const auto& entity : document.entities) {
        PreparedEntity value {.id = entity.id, .parent = entity.parent};
        for (const auto& component : entity.components) {
            auto type = Registry::instance().try_get_type(component.type);
            if (!type) {
                warnings.push_back(
                    "Unknown component '" + component.type + "' on entity " +
                    entity.id.as_string()
                );
                continue;
            }
            if (!type->has_annotation<annotations::Component>()) {
                return failure(scene_error(
                    SceneDocumentErrorKind::DeserializeComponent,
                    "$.entities[" + std::to_string(entity_index) +
                        "].components." + component.type,
                    "Reflected type is not tagged as a Component"
                ));
            }
            auto decoded = serialization::deserialize(
                type->id(),
                component.properties,
                serialization::DeserializeOptions {.codecs = codecs}
            );
            if (!decoded) {
                return failure(scene_error(
                    SceneDocumentErrorKind::DeserializeComponent,
                    "$.entities[" + std::to_string(entity_index) +
                        "].components." + component.type,
                    decoded.error().message
                ));
            }
            value.components.push_back(std::move(*decoded));
        }
        prepared.push_back(std::move(value));
        ++entity_index;
    }

    SceneInstantiationResult result {.warnings = std::move(warnings)};
    for (auto& entity : prepared) {
        const auto runtime_entity = world.entity();
        result.bindings.bind(entity.id, runtime_entity);
        for (auto& component : entity.components) {
            world.add_component(runtime_entity, component.ref());
        }
    }
    for (const auto& entity : prepared) {
        if (!entity.parent) {
            continue;
        }
        world.set_parent(
            *result.bindings.entity(entity.id),
            *result.bindings.entity(*entity.parent)
        );
    }
    return result;
}

AssetLoadResult<SceneDocument>
SceneDocumentLoader::load(Reader& reader, const LoadContext& context) {
    auto document = parse_scene_document(reader.as_string_view());
    if (!document) {
        return failure(AssetLoadError(
            context.asset_path(),
            document.error().path + ": " + document.error().message
        ));
    }
    return std::make_unique<SceneDocument>(std::move(*document));
}

} // namespace ets
