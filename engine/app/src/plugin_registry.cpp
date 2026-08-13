#include "app/plugin_registry.hpp"

#include "app/app.hpp"
#include "refl/generated.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace fei {

PluginRegistry& PluginRegistry::instance() {
    static PluginRegistry registry;
    return registry;
}

void PluginRegistry::add(PluginDescriptor descriptor) {
    if (descriptor.name.empty()) {
        throw std::runtime_error("Plugin name cannot be empty");
    }
    if (!descriptor.create) {
        throw std::runtime_error(
            "Plugin '" + descriptor.name + "' has no factory"
        );
    }
    auto existing = m_descriptors.find(descriptor.name);
    if (existing != m_descriptors.end()) {
        if (existing->second.type != descriptor.type) {
            throw std::runtime_error(
                "Plugin name '" + descriptor.name +
                "' is registered for both '" + existing->second.type_name +
                "' and '" + descriptor.type_name + "'"
            );
        }
        return;
    }
    const auto same_type =
        std::ranges::find_if(m_descriptors, [&](const auto& entry) {
            return entry.second.type == descriptor.type;
        });
    if (same_type != m_descriptors.end()) {
        throw std::runtime_error(
            "Plugin type '" + descriptor.type_name +
            "' is registered with both names '" + same_type->second.name +
            "' and '" + descriptor.name + "'"
        );
    }
    m_descriptors.emplace(descriptor.name, std::move(descriptor));
}

const PluginDescriptor* PluginRegistry::find(std::string_view name) const {
    auto descriptor = m_descriptors.find(std::string(name));
    if (descriptor == m_descriptors.end()) {
        return nullptr;
    }
    return &descriptor->second;
}

App& App::add_plugin(std::string_view name) {
    register_generated_reflection();
    const auto* descriptor = PluginRegistry::instance().find(name);
    if (!descriptor) {
        throw std::runtime_error("Unknown plugin '" + std::string(name) + "'");
    }
    return add_boxed_plugin(
        descriptor->type,
        descriptor->name,
        descriptor->create()
    );
}

} // namespace fei
