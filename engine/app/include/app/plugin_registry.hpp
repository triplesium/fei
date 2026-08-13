#pragma once

#include "app/plugin.hpp"

#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace fei {

class PluginId {
  private:
    std::string m_qualified_name;

  public:
    explicit PluginId(std::string qualified_name);

    [[nodiscard]] std::string_view qualified_name() const;
    [[nodiscard]] std::string_view namespace_name() const;
    [[nodiscard]] std::string_view local_name() const;
};

struct PluginDescriptor {
    PluginId id;
    TypeId type;
    std::string type_name;
    std::unique_ptr<Plugin> (*create)() {nullptr};
};

class PluginRegistry {
  private:
    std::unordered_map<std::string, PluginDescriptor> m_descriptors;

  public:
    static PluginRegistry& instance();

    void add(PluginDescriptor descriptor);
    const PluginDescriptor* find(std::string_view name) const;
};

template<typename T>
    requires std::derived_from<T, Plugin>
void register_generated_plugin(std::string name) {
    static_assert(
        std::default_initializable<T>,
        "Reflected plugin types must be default constructible"
    );
    PluginRegistry::instance().add(
        PluginDescriptor {
            .id = PluginId {std::move(name)},
            .type = type_id<T>(),
            .type_name = std::string(fei::type_name<T>()),
            .create = []() -> std::unique_ptr<Plugin> {
                return std::make_unique<T>();
            },
        }
    );
}

} // namespace fei
