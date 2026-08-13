#pragma once

#include "refl/reflect.hpp" // IWYU pragma: export
#include "refl/type.hpp"

#include <concepts>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace fei {

class App;
class PluginDependencies;

class Plugin {
  public:
    virtual ~Plugin() = default;
    virtual void dependencies(PluginDependencies& /*dependencies*/) const {}
    virtual void setup(App& app) = 0;
    virtual void finish(App& app) {}
    virtual void cleanup(App& app) noexcept {}
};

struct PluginRequirement {
    TypeId type;
    std::string name;
    std::string required_by;
    std::function<std::unique_ptr<Plugin>()> create_default;
    bool configured {false};
};

class PluginDependencies {
  public:
    template<std::derived_from<Plugin> T>
    PluginDependencies& require() {
        const auto type = type_id<T>();
        for (const auto& requirement : m_requirements) {
            if (requirement.type == type) {
                return *this;
            }
        }
        PluginRequirement requirement {
            .type = type,
            .name = std::string(type_name<T>()),
            .required_by = {},
            .create_default = {},
            .configured = false,
        };
        if constexpr (std::default_initializable<T>) {
            requirement.create_default = []() -> std::unique_ptr<Plugin> {
                return std::make_unique<T>();
            };
        }
        m_requirements.push_back(std::move(requirement));
        return *this;
    }

    template<typename P>
        requires std::derived_from<std::remove_cvref_t<P>, Plugin> &&
                 std::constructible_from<std::remove_cvref_t<P>, P>
    PluginDependencies& require(P&& plugin) {
        using T = std::remove_cvref_t<P>;
        const auto type = type_id<T>();
        for (const auto& requirement : m_requirements) {
            if (requirement.type == type) {
                return *this;
            }
        }
        auto configured_plugin = std::make_shared<T>(std::forward<P>(plugin));
        m_requirements.push_back(
            PluginRequirement {
                .type = type,
                .name = std::string(type_name<T>()),
                .required_by = {},
                .create_default =
                    [configured_plugin]() mutable -> std::unique_ptr<Plugin> {
                    return std::make_unique<T>(std::move(*configured_plugin));
                },
                .configured = true,
            }
        );
        return *this;
    }

    const std::vector<PluginRequirement>& requirements() const {
        return m_requirements;
    }

  private:
    std::vector<PluginRequirement> m_requirements;
};

} // namespace fei
