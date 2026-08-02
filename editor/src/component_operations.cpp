#include "editor/component_operations.hpp"

#include "ecs/type_tags.hpp"
#include "ecs/world.hpp"
#include "refl/registry.hpp"
#include "refl/val.hpp"
#include "serialization/json_archive.hpp"

#include <utility>

namespace fei::editor {

namespace {

ComponentError make_error(
    ComponentError::Kind kind,
    Entity entity,
    TypeId type,
    std::string message
) {
    return ComponentError {
        .kind = kind,
        .entity = entity,
        .type = type,
        .message = std::move(message),
    };
}

Result<Type*, ComponentError> component_type(Entity entity, TypeId type) {
    auto reflected_type = Registry::instance().try_get_type(type);
    if (!reflected_type) {
        return failure(make_error(
            ComponentError::Kind::TypeNotFound,
            entity,
            type,
            reflected_type.error().message
        ));
    }
    if (!reflected_type->has_tag(ComponentTypeTag)) {
        return failure(make_error(
            ComponentError::Kind::NotComponent,
            entity,
            type,
            "Reflected type is not tagged as a Component"
        ));
    }
    return &*reflected_type;
}

std::string node_preview(const serialization::SerializedNode& node) {
    if (node.is_null()) {
        return "None";
    }
    if (const auto* value = node.try_string()) {
        return *value;
    }
    if (const auto* value = node.try_bool()) {
        return *value ? "true" : "false";
    }
    if (const auto* value = node.try_signed_integer()) {
        return std::to_string(*value);
    }
    if (const auto* value = node.try_unsigned_integer()) {
        return std::to_string(*value);
    }
    if (const auto* value = node.try_floating()) {
        return std::to_string(*value);
    }
    if (const auto* object = node.try_object(); object && object->size() == 1) {
        const auto& value = object->front().value;
        if (!value.is_array() && !value.is_object()) {
            return node_preview(value);
        }
    }

    auto json = serialization::write_json(node, 0);
    return json ? std::move(*json) : "Unavailable";
}

} // namespace

Optional<std::string> ComponentOperations::preview(Ref value) const {
    if (!value) {
        return nullopt;
    }
    const auto* codec = m_codecs.find(value.type_id());
    if (!codec) {
        return nullopt;
    }

    auto node = codec->encode(value, "$");
    if (!node) {
        return "Unavailable: " + node.error().message;
    }
    return node_preview(*node);
}

Status<ComponentError> ComponentOperations::add_default(
    World& world,
    Entity entity,
    TypeId type
) const {
    auto reflected_type = component_type(entity, type);
    if (!reflected_type) {
        return failure(std::move(reflected_type.error()));
    }
    if (!world.has_entity(entity)) {
        return failure(make_error(
            ComponentError::Kind::EntityNotFound,
            entity,
            type,
            "Entity does not exist"
        ));
    }
    if (world.has_component(entity, type)) {
        return {};
    }
    if (!(*reflected_type)->default_constructible()) {
        return failure(make_error(
            ComponentError::Kind::NotDefaultConstructible,
            entity,
            type,
            "Component '" + (*reflected_type)->stripped_name() +
                "' is not default constructible"
        ));
    }

    auto value = Val::default_construct(**reflected_type);
    world.add_component(entity, value.ref());
    return {};
}

Status<ComponentError>
ComponentOperations::remove(World& world, Entity entity, TypeId type) const {
    auto reflected_type = component_type(entity, type);
    if (!reflected_type) {
        return failure(std::move(reflected_type.error()));
    }
    if (!world.has_entity(entity)) {
        return failure(make_error(
            ComponentError::Kind::EntityNotFound,
            entity,
            type,
            "Entity does not exist"
        ));
    }
    if (!world.has_component(entity, type)) {
        return failure(make_error(
            ComponentError::Kind::ComponentNotFound,
            entity,
            type,
            "Entity does not contain the component"
        ));
    }

    world.remove_component(entity, type);
    return {};
}

Result<serialization::SerializedNode, ComponentError>
ComponentOperations::serialize(
    const World& world,
    Entity entity,
    TypeId type
) const {
    auto reflected_type = component_type(entity, type);
    if (!reflected_type) {
        return failure(std::move(reflected_type.error()));
    }
    if (!world.has_entity(entity)) {
        return failure(make_error(
            ComponentError::Kind::EntityNotFound,
            entity,
            type,
            "Entity does not exist"
        ));
    }
    if (!world.has_component(entity, type)) {
        return failure(make_error(
            ComponentError::Kind::ComponentNotFound,
            entity,
            type,
            "Entity does not contain the component"
        ));
    }

    auto node = serialization::serialize(
        world.get_component(entity, type),
        serialization::SerializeOptions {.codecs = &m_codecs}
    );
    if (!node) {
        return failure(make_error(
            ComponentError::Kind::SerializeFailed,
            entity,
            type,
            node.error().message
        ));
    }
    return std::move(*node);
}

Status<ComponentError> ComponentOperations::set(
    World& world,
    Entity entity,
    TypeId type,
    const serialization::SerializedNode& node
) const {
    auto reflected_type = component_type(entity, type);
    if (!reflected_type) {
        return failure(std::move(reflected_type.error()));
    }
    if (!world.has_entity(entity)) {
        return failure(make_error(
            ComponentError::Kind::EntityNotFound,
            entity,
            type,
            "Entity does not exist"
        ));
    }

    auto value = serialization::deserialize(
        type,
        node,
        serialization::DeserializeOptions {.codecs = &m_codecs}
    );
    if (!value) {
        return failure(make_error(
            ComponentError::Kind::DeserializeFailed,
            entity,
            type,
            value.error().message
        ));
    }

    world.add_component(entity, value->ref());
    return {};
}

} // namespace fei::editor
