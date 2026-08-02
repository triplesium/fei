#include "editor/component_registry.hpp"

#include "ecs/world.hpp"
#include "refl/val.hpp"
#include "serialization/json_archive.hpp"

#include <algorithm>
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

bool ComponentRegistry::register_component(TypeId type, std::string name) {
    if (!type || name.empty() || find(type) != nullptr) {
        return false;
    }
    m_entries.push_back(ComponentInfo {.type = type, .name = std::move(name)});
    std::ranges::sort(m_entries, {}, &ComponentInfo::name);
    return true;
}

const ComponentInfo* ComponentRegistry::find(TypeId type) const {
    const auto entry = std::ranges::find(m_entries, type, &ComponentInfo::type);
    return entry == m_entries.end() ? nullptr : &*entry;
}

Optional<std::string> ComponentRegistry::preview(Ref value) const {
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

Status<ComponentError>
ComponentRegistry::add_default(World& world, Entity entity, TypeId type) const {
    const auto* component = find(type);
    if (!component) {
        return failure(make_error(
            ComponentError::Kind::NotRegistered,
            entity,
            type,
            "Component type is not registered with the editor"
        ));
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

    auto reflected_type = Registry::instance().try_get_type(type);
    if (!reflected_type) {
        return failure(make_error(
            ComponentError::Kind::TypeNotFound,
            entity,
            type,
            reflected_type.error().message
        ));
    }
    if (!reflected_type->default_constructible()) {
        return failure(make_error(
            ComponentError::Kind::NotDefaultConstructible,
            entity,
            type,
            "Component '" + component->name + "' is not default constructible"
        ));
    }

    auto value = Val::default_construct(*reflected_type);
    world.add_component(entity, value.ref());
    return {};
}

Status<ComponentError>
ComponentRegistry::remove(World& world, Entity entity, TypeId type) const {
    if (!find(type)) {
        return failure(make_error(
            ComponentError::Kind::NotRegistered,
            entity,
            type,
            "Component type is not registered with the editor"
        ));
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
ComponentRegistry::serialize(
    const World& world,
    Entity entity,
    TypeId type
) const {
    if (!find(type)) {
        return failure(make_error(
            ComponentError::Kind::NotRegistered,
            entity,
            type,
            "Component type is not registered with the editor"
        ));
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

Status<ComponentError> ComponentRegistry::set(
    World& world,
    Entity entity,
    TypeId type,
    const serialization::SerializedNode& node
) const {
    if (!find(type)) {
        return failure(make_error(
            ComponentError::Kind::NotRegistered,
            entity,
            type,
            "Component type is not registered with the editor"
        ));
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
