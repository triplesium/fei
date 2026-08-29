#pragma once

#include "app/plugin_id.hpp"
#include "refl/reflect.hpp" // IWYU pragma: export
#include "refl/type.hpp"

#include <concepts>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ets {

namespace annotations {

ETS_ANNOTATION(Plugin)
struct Plugin {
    std::string name;
};

} // namespace annotations

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

class PluginKey {
  private:
    TypeId m_type;
    std::string m_runtime_id;

  public:
    explicit PluginKey(TypeId type) : m_type(type) {}
    explicit PluginKey(const PluginId& id) :
        m_runtime_id(id.qualified_name()) {}

    [[nodiscard]] bool is_runtime() const { return !m_runtime_id.empty(); }
    [[nodiscard]] TypeId type() const { return m_type; }
    [[nodiscard]] std::string_view runtime_id() const { return m_runtime_id; }

    bool operator==(const PluginKey& other) const {
        if (is_runtime() != other.is_runtime()) {
            return false;
        }
        return is_runtime() ? m_runtime_id == other.m_runtime_id :
                              m_type == other.m_type;
    }
};

struct PluginKeyHash {
    std::size_t operator()(const PluginKey& key) const {
        if (key.is_runtime()) {
            return std::hash<std::string_view> {}(key.runtime_id());
        }
        return std::hash<std::uint64_t> {}(key.type().id());
    }
};

struct PluginRequirement {
    PluginKey key;
    TypeId implementation_type;
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
            if (requirement.key == PluginKey {type}) {
                return *this;
            }
        }
        PluginRequirement requirement {
            .key = PluginKey {type},
            .implementation_type = type,
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
            if (requirement.key == PluginKey {type}) {
                return *this;
            }
        }
        auto configured_plugin = std::make_shared<T>(std::forward<P>(plugin));
        m_requirements.push_back(
            PluginRequirement {
                .key = PluginKey {type},
                .implementation_type = type,
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

    template<typename P>
        requires std::derived_from<std::remove_cvref_t<P>, Plugin> &&
                 std::constructible_from<std::remove_cvref_t<P>, P>
    PluginDependencies& require(const PluginId& id, P&& plugin) {
        using T = std::remove_cvref_t<P>;
        const PluginKey key {id};
        for (const auto& requirement : m_requirements) {
            if (requirement.key == key) {
                return *this;
            }
        }
        auto configured_plugin = std::make_shared<T>(std::forward<P>(plugin));
        m_requirements.push_back(
            PluginRequirement {
                .key = key,
                .implementation_type = type_id<T>(),
                .name = std::string(id.qualified_name()),
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

} // namespace ets
