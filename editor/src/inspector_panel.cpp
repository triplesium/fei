#include "editor/inspector_panel.hpp"

#include "base/optional.hpp"
#include "ecs/type_tags.hpp"
#include "ecs/world.hpp"
#include "refl/cls.hpp"
#include "refl/property.hpp"
#include "refl/ref.hpp"
#include "refl/registry.hpp"

#include <algorithm>
#include <imgui.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fei::editor {

namespace {

std::string reflected_type_name(TypeId type) {
    auto reflected_type = Registry::instance().try_get_type(type);
    return reflected_type ? reflected_type->stripped_name() :
                            "Type " + std::to_string(type.id());
}

struct ReflectedComponent {
    TypeId type;
    std::string name;
    bool default_constructible {false};
};

std::vector<ReflectedComponent>
reflected_components(const std::vector<TypeId>& types) {
    std::vector<ReflectedComponent> components;
    components.reserve(types.size());
    for (const auto type : types) {
        auto reflected_type = Registry::instance().try_get_type(type);
        if (!reflected_type || !reflected_type->has_tag(ComponentTypeTag)) {
            continue;
        }
        components.push_back(
            ReflectedComponent {
                .type = type,
                .name = reflected_type->stripped_name(),
                .default_constructible =
                    reflected_type->default_constructible(),
            }
        );
    }
    std::ranges::sort(components, {}, &ReflectedComponent::name);
    return components;
}

std::vector<ReflectedComponent> reflected_components() {
    return reflected_components(
        Registry::instance().types_with_tag(ComponentTypeTag)
    );
}

void draw_field_label(std::string_view label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::SameLine(130.0f);
    ImGui::SetNextItemWidth(-1.0f);
}

bool draw_reflected_value(
    Ref value,
    std::string_view label,
    int depth,
    const ComponentOperations& operations
);

bool draw_reflected_object(
    Ref value,
    int depth,
    const ComponentOperations& operations
) {
    if (depth > 8) {
        ImGui::TextDisabled("Reflection depth limit reached");
        return false;
    }

    auto cls = Registry::instance().try_get_cls(value.type_id());
    if (!cls) {
        return false;
    }

    auto properties = cls->get_properties();
    std::ranges::sort(properties, {}, [](const Property* property) {
        return property->name();
    });

    bool changed = false;
    for (const auto* property : properties) {
        auto property_value = property->get(value);
        if (!property_value) {
            ImGui::TextDisabled("%s: unavailable", property->name().c_str());
            continue;
        }
        changed |= draw_reflected_value(
            *property_value,
            property->name(),
            depth + 1,
            operations
        );
    }
    return changed;
}

bool draw_reflected_value(
    Ref value,
    std::string_view label,
    int depth,
    const ComponentOperations& operations
) {
    const std::string label_string(label);
    ImGui::PushID(label_string.c_str());

    bool changed = false;
    if (auto* data = value.try_get<bool>()) {
        draw_field_label(label);
        changed = ImGui::Checkbox("##value", data);
    } else if (auto* data = value.try_get<float>()) {
        draw_field_label(label);
        changed = ImGui::DragFloat("##value", data, 0.05f);
    } else if (auto* data = value.try_get<double>()) {
        draw_field_label(label);
        changed =
            ImGui::DragScalar("##value", ImGuiDataType_Double, data, 0.05f);
    } else if (auto* data = value.try_get<int>()) {
        draw_field_label(label);
        changed = ImGui::DragInt("##value", data, 0.1f);
    } else if (auto* data = value.try_get<unsigned int>()) {
        draw_field_label(label);
        changed = ImGui::DragScalar("##value", ImGuiDataType_U32, data, 0.1f);
    } else if (auto preview = operations.preview(value)) {
        draw_field_label(label);
        ImGui::TextUnformatted(preview->c_str());
    } else if (Registry::instance().try_get_cls(value.type_id())) {
        const auto flags =
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        const bool open = ImGui::TreeNodeEx(label_string.c_str(), flags);
        if (open) {
            changed = draw_reflected_object(value, depth, operations);
            ImGui::TreePop();
        }
    } else {
        draw_field_label(label);
        ImGui::TextDisabled("%s", reflected_type_name(value.type_id()).c_str());
    }

    ImGui::PopID();
    return changed;
}

void record_component_result(
    ActivityLog& activity,
    std::string action,
    Entity entity,
    std::string_view component_name,
    const Status<ComponentError>& result
) {
    const auto detail = "Entity " + std::to_string(entity) + " / " +
                        std::string(component_name) +
                        (result ? "" : ": " + result.error().message);
    activity.record(
        OperationSource::User,
        std::move(action),
        detail,
        result.has_value()
    );
}

} // namespace

void InspectorPanel::draw(InspectorPanelContext context) {
    if (!m_open) {
        return;
    }
    if (!ImGui::Begin("Inspector")) {
        ImGui::End();
        return;
    }

    if (context.selection.entity &&
        !context.world.has_entity(*context.selection.entity)) {
        context.selection.entity = nullopt;
    }
    if (!context.selection.entity && context.selection.asset) {
        if (context.draw_asset) {
            context.draw_asset(*context.selection.asset);
        }
        ImGui::End();
        return;
    }
    if (!context.selection.entity) {
        ImGui::TextDisabled("Select an entity to inspect it.");
        ImGui::End();
        return;
    }

    const auto entity = *context.selection.entity;
    ImGui::Text("Entity %u", entity);
    ImGui::Separator();

    Optional<TypeId> pending_remove;
    const auto location = context.world.entity_location(entity);
    const auto component_list = reflected_components(
        context.world.archetypes().get(location->archetype_id).components()
    );
    for (const auto& component : component_list) {
        ImGui::PushID(static_cast<int>(component.type.id()));
        const bool open = ImGui::CollapsingHeader(
            component.name.c_str(),
            ImGuiTreeNodeFlags_DefaultOpen
        );
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 4.0f);
        if (ImGui::SmallButton("x")) {
            pending_remove = component.type;
        }
        if (open) {
            auto value = context.world.get_component(entity, component.type);
            if (draw_reflected_object(value, 0, context.operations)) {
                context.world.mark_component_changed(entity, component.type);
                context.activity.record(
                    OperationSource::User,
                    "SetComponentProperty",
                    "Entity " + std::to_string(entity) + " / " + component.name
                );
            }
            ImGui::Spacing();
        }
        ImGui::PopID();
    }

    if (pending_remove) {
        const auto component_name = reflected_type_name(*pending_remove);
        const auto result =
            context.operations.remove(context.world, entity, *pending_remove);
        record_component_result(
            context.activity,
            "RemoveComponent",
            entity,
            component_name,
            result
        );
    }

    if (ImGui::Button("Add Component")) {
        ImGui::OpenPopup("add_component");
    }
    if (ImGui::BeginPopup("add_component")) {
        for (const auto& component : reflected_components()) {
            if (context.world.has_component(entity, component.type)) {
                continue;
            }
            if (ImGui::MenuItem(
                    component.name.c_str(),
                    nullptr,
                    false,
                    component.default_constructible
                )) {
                const auto result = context.operations.add_default(
                    context.world,
                    entity,
                    component.type
                );
                record_component_result(
                    context.activity,
                    "AddComponent",
                    entity,
                    component.name,
                    result
                );
            }
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace fei::editor
