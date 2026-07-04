#include "app/plugin_group.hpp"

#include "app/app.hpp"
#include "base/log.hpp"

#include <algorithm>
#include <iterator>

namespace fei {

PluginGroupBuilder::PluginGroupBuilder(std::string name) :
    m_name(std::move(name)) {}

std::vector<PluginGroupBuilder::PluginEntry>::iterator
PluginGroupBuilder::find_plugin(TypeId type) {
    return std::ranges::find_if(m_plugins, [type](const PluginEntry& entry) {
        return entry.type == type;
    });
}

std::vector<PluginGroupBuilder::PluginEntry>::const_iterator
PluginGroupBuilder::find_plugin(TypeId type) const {
    return std::ranges::find_if(m_plugins, [type](const PluginEntry& entry) {
        return entry.type == type;
    });
}

void PluginGroupBuilder::add_impl(PluginEntry entry) {
    auto existing = find_plugin(entry.type);
    if (existing != m_plugins.end()) {
        m_plugins.erase(existing);
    }
    m_plugins.push_back(std::move(entry));
}

void PluginGroupBuilder::set_impl(PluginEntry entry) {
    auto existing = find_plugin(entry.type);
    if (existing == m_plugins.end()) {
        fatal(
            "PluginGroup {} cannot set missing plugin {}",
            m_name,
            entry.name
        );
    }

    existing->name = std::move(entry.name);
    existing->plugin = std::move(entry.plugin);
}

void PluginGroupBuilder::enable_impl(
    TypeId plugin_type,
    std::string_view plugin_name
) {
    auto existing = find_plugin(plugin_type);
    if (existing == m_plugins.end()) {
        fatal(
            "PluginGroup {} cannot enable missing plugin {}",
            m_name,
            plugin_name
        );
    }
    existing->enabled = true;
}

void PluginGroupBuilder::disable_impl(
    TypeId plugin_type,
    std::string_view plugin_name
) {
    auto existing = find_plugin(plugin_type);
    if (existing == m_plugins.end()) {
        fatal(
            "PluginGroup {} cannot disable missing plugin {}",
            m_name,
            plugin_name
        );
    }
    existing->enabled = false;
}

void PluginGroupBuilder::add_before_impl(
    TypeId target_type,
    std::string_view target_name,
    PluginEntry entry
) {
    auto target = find_plugin(target_type);
    if (target == m_plugins.end()) {
        fatal(
            "PluginGroup {} cannot add plugin before missing target {}",
            m_name,
            target_name
        );
    }

    auto target_index = std::distance(m_plugins.begin(), target);
    auto existing = find_plugin(entry.type);
    if (existing != m_plugins.end()) {
        auto existing_index = std::distance(m_plugins.begin(), existing);
        if (existing_index == target_index) {
            *existing = std::move(entry);
            return;
        }
        m_plugins.erase(existing);
        if (existing_index < target_index) {
            --target_index;
        }
    }

    m_plugins.insert(m_plugins.begin() + target_index, std::move(entry));
}

void PluginGroupBuilder::add_after_impl(
    TypeId target_type,
    std::string_view target_name,
    PluginEntry entry
) {
    auto target = find_plugin(target_type);
    if (target == m_plugins.end()) {
        fatal(
            "PluginGroup {} cannot add plugin after missing target {}",
            m_name,
            target_name
        );
    }

    auto target_index = std::distance(m_plugins.begin(), target);
    auto existing = find_plugin(entry.type);
    if (existing != m_plugins.end()) {
        auto existing_index = std::distance(m_plugins.begin(), existing);
        if (existing_index == target_index) {
            *existing = std::move(entry);
            return;
        }
        m_plugins.erase(existing);
        if (existing_index < target_index) {
            --target_index;
        }
    }

    m_plugins.insert(m_plugins.begin() + target_index + 1, std::move(entry));
}

void PluginGroupBuilder::finish(App& app) {
    for (auto& entry : m_plugins) {
        if (!entry.enabled) {
            continue;
        }
        app.add_boxed_plugin(entry.type, entry.name, std::move(entry.plugin));
    }
}

} // namespace fei
