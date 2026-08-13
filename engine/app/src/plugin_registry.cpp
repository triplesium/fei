#include "app/plugin_registry.hpp"

#include "app/app.hpp"
#include "refl/generated.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fei {

PluginRegistry& detail::plugin_registry_storage() {
    static PluginRegistry registry;
    return registry;
}

PluginRegistry& plugin_registry() {
    register_generated_reflection();
    return detail::plugin_registry_storage();
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
    return find(PluginId {std::string(name)});
}

const PluginDescriptor* PluginRegistry::find(const PluginId& id) const {
    auto descriptor = m_descriptors.find(std::string(id.qualified_name()));
    if (descriptor == m_descriptors.end()) {
        return nullptr;
    }
    return &descriptor->second;
}

std::vector<const PluginDescriptor*> PluginRegistry::plugins() const {
    std::vector<const PluginDescriptor*> result;
    result.reserve(m_descriptors.size());
    for (const auto& descriptor : m_descriptors) {
        result.push_back(&descriptor.second);
    }
    std::ranges::sort(result, {}, [](const PluginDescriptor* descriptor) {
        return descriptor->id.qualified_name();
    });
    return result;
}

namespace {

[[nodiscard]] std::size_t
edit_distance(std::string_view left, std::string_view right) {
    std::vector<std::size_t> previous(right.size() + 1);
    std::vector<std::size_t> current(right.size() + 1);
    for (std::size_t index = 0; index <= right.size(); ++index) {
        previous[index] = index;
    }
    for (std::size_t left_index = 0; left_index < left.size(); ++left_index) {
        current[0] = left_index + 1;
        for (std::size_t right_index = 0; right_index < right.size();
             ++right_index) {
            const auto substitution =
                previous[right_index] +
                (left[left_index] == right[right_index] ? 0U : 1U);
            current[right_index + 1] = std::min(
                {previous[right_index + 1] + 1,
                 current[right_index] + 1,
                 substitution}
            );
        }
        std::swap(previous, current);
    }
    return previous.back();
}

[[nodiscard]] std::string unknown_plugin_message(
    const PluginRegistry& registry,
    const PluginId& requested
) {
    std::vector<const PluginDescriptor*> candidates;
    for (const auto* descriptor : registry.plugins()) {
        const auto same_namespace =
            descriptor->id.namespace_name() == requested.namespace_name();
        const auto distance =
            edit_distance(descriptor->id.local_name(), requested.local_name());
        const auto fuzzy_limit =
            std::max<std::size_t>(2, requested.local_name().size() / 3);
        if (same_namespace || distance <= fuzzy_limit) {
            candidates.push_back(descriptor);
        }
    }

    std::string message =
        "Unknown plugin '" + std::string(requested.qualified_name()) + "'";
    if (candidates.empty()) {
        return message;
    }
    message += ". Available candidates: ";
    constexpr std::size_t c_max_candidates = 5;
    for (std::size_t index = 0;
         index < std::min(candidates.size(), c_max_candidates);
         ++index) {
        if (index != 0) {
            message += ", ";
        }
        message +=
            "'" + std::string(candidates[index]->id.qualified_name()) + "'";
    }
    return message;
}

} // namespace

App& App::add_plugin(std::string_view name) {
    return add_plugin(PluginId {std::string(name)});
}

App& App::add_plugin(const PluginId& id) {
    const auto& registry = plugin_registry();
    const auto* descriptor = registry.find(id);
    if (!descriptor) {
        throw std::runtime_error(unknown_plugin_message(registry, id));
    }
    return add_boxed_plugin(
        descriptor->type,
        descriptor->id.qualified_name(),
        descriptor->create()
    );
}

} // namespace fei
