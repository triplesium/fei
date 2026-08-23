#pragma once

#include "app/plugin.hpp"
#include "app/plugin_id.hpp"

#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ets {

struct PluginDescriptor {
    PluginId id;
    TypeId type;
    std::string type_name;
    std::unique_ptr<Plugin> (*create)() {nullptr};

    [[nodiscard]] bool is_constructible() const { return create != nullptr; }
};

class PluginRegistry {
  private:
    std::unordered_map<std::string, PluginDescriptor> m_descriptors;

  public:
    PluginRegistry() = default;

    void add(PluginDescriptor descriptor);
    [[nodiscard]] const PluginDescriptor* find(const PluginId& id) const;
    [[nodiscard]] const PluginDescriptor* find(std::string_view name) const;
    [[nodiscard]] std::vector<const PluginDescriptor*> plugins() const;
};

[[nodiscard]] PluginRegistry& plugin_registry();

namespace detail {
[[nodiscard]] PluginRegistry& plugin_registry_storage();
} // namespace detail

template<typename T>
    requires std::derived_from<T, Plugin>
void register_generated_plugin(std::string name) {
    static_assert(
        std::default_initializable<T>,
        "Reflected plugin types must be default constructible"
    );
    detail::plugin_registry_storage().add(
        PluginDescriptor {
            .id = PluginId {std::move(name)},
            .type = type_id<T>(),
            .type_name = std::string(ets::type_name<T>()),
            .create = []() -> std::unique_ptr<Plugin> {
                return std::make_unique<T>();
            },
        }
    );
}

} // namespace ets
