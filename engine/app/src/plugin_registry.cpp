#include "app/plugin_registry.hpp"

#include "app/app.hpp"
#include "refl/generated.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace fei {

PluginId::PluginId(std::string qualified_name) :
    m_qualified_name(std::move(qualified_name)) {
    if (m_qualified_name.empty()) {
        throw std::runtime_error("Plugin id cannot be empty");
    }
    if (m_qualified_name.ends_with("::")) {
        throw std::runtime_error(
            "Invalid qualified plugin id '" + m_qualified_name + "'"
        );
    }

    for (std::size_t index = 0; index < m_qualified_name.size(); ++index) {
        if (m_qualified_name[index] != ':') {
            continue;
        }
        if (index == 0 || index + 1 >= m_qualified_name.size() ||
            m_qualified_name[index + 1] != ':' ||
            m_qualified_name[index - 1] == ':') {
            throw std::runtime_error(
                "Invalid qualified plugin id '" + m_qualified_name + "'"
            );
        }
        index += 1;
    }
}

std::string_view PluginId::qualified_name() const {
    return std::string_view(m_qualified_name);
}

std::string_view PluginId::namespace_name() const {
    const auto separator = m_qualified_name.rfind("::");
    if (separator == std::string::npos) {
        return {};
    }
    return std::string_view(m_qualified_name).substr(0, separator);
}

std::string_view PluginId::local_name() const {
    const auto separator = m_qualified_name.rfind("::");
    if (separator == std::string::npos) {
        return std::string_view(m_qualified_name);
    }
    return std::string_view(m_qualified_name).substr(separator + 2);
}

PluginRegistry& PluginRegistry::instance() {
    static PluginRegistry registry;
    return registry;
}

void PluginRegistry::add(PluginDescriptor descriptor) {
    const auto qualified_name = std::string(descriptor.id.qualified_name());
    if (!descriptor.create) {
        throw std::runtime_error(
            "Plugin '" + qualified_name + "' has no factory"
        );
    }
    auto existing = m_descriptors.find(qualified_name);
    if (existing != m_descriptors.end()) {
        if (existing->second.type != descriptor.type) {
            throw std::runtime_error(
                "Plugin id '" + qualified_name + "' is registered for both '" +
                existing->second.type_name + "' and '" + descriptor.type_name +
                "'"
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
            "' is registered with both ids '" +
            std::string(same_type->second.id.qualified_name()) + "' and '" +
            qualified_name + "'"
        );
    }
    m_descriptors.emplace(qualified_name, std::move(descriptor));
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
        descriptor->id.qualified_name(),
        descriptor->create()
    );
}

} // namespace fei
