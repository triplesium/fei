#pragma once

#include "app/plugin.hpp"
#include "refl/type.hpp"

#include <concepts>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace fei {

class App;

class PluginGroupBuilder {
  private:
    struct PluginEntry {
        TypeId type;
        std::string name;
        std::unique_ptr<Plugin> plugin;
        bool enabled {true};
    };

  public:
    explicit PluginGroupBuilder(std::string name);
    PluginGroupBuilder(const PluginGroupBuilder&) = delete;
    PluginGroupBuilder& operator=(const PluginGroupBuilder&) = delete;
    PluginGroupBuilder(PluginGroupBuilder&&) noexcept = default;
    PluginGroupBuilder& operator=(PluginGroupBuilder&&) noexcept = default;
    ~PluginGroupBuilder() = default;

    template<typename Group>
    static PluginGroupBuilder start() {
        return PluginGroupBuilder(std::string(type_name<Group>()));
    }

    const std::string& name() const { return m_name; }

    template<typename P>
        requires std::derived_from<std::remove_cvref_t<P>, Plugin>
    PluginGroupBuilder& add(P&& plugin) & {
        add_impl(make_entry(std::forward<P>(plugin)));
        return *this;
    }

    template<typename P>
        requires std::derived_from<std::remove_cvref_t<P>, Plugin>
    PluginGroupBuilder&& add(P&& plugin) && {
        add_impl(make_entry(std::forward<P>(plugin)));
        return std::move(*this);
    }

    template<typename P>
        requires std::derived_from<std::remove_cvref_t<P>, Plugin>
    PluginGroupBuilder& set(P&& plugin) & {
        set_impl(make_entry(std::forward<P>(plugin)));
        return *this;
    }

    template<typename P>
        requires std::derived_from<std::remove_cvref_t<P>, Plugin>
    PluginGroupBuilder&& set(P&& plugin) && {
        set_impl(make_entry(std::forward<P>(plugin)));
        return std::move(*this);
    }

    template<std::derived_from<Plugin> T>
    PluginGroupBuilder& enable() & {
        enable_impl(type_id<T>(), type_name<T>());
        return *this;
    }

    template<std::derived_from<Plugin> T>
    PluginGroupBuilder&& enable() && {
        enable_impl(type_id<T>(), type_name<T>());
        return std::move(*this);
    }

    template<std::derived_from<Plugin> T>
    PluginGroupBuilder& disable() & {
        disable_impl(type_id<T>(), type_name<T>());
        return *this;
    }

    template<std::derived_from<Plugin> T>
    PluginGroupBuilder&& disable() && {
        disable_impl(type_id<T>(), type_name<T>());
        return std::move(*this);
    }

    template<std::derived_from<Plugin> Target, typename P>
        requires std::derived_from<std::remove_cvref_t<P>, Plugin>
    PluginGroupBuilder& add_before(P&& plugin) & {
        add_before_impl(
            type_id<Target>(),
            type_name<Target>(),
            make_entry(std::forward<P>(plugin))
        );
        return *this;
    }

    template<std::derived_from<Plugin> Target, typename P>
        requires std::derived_from<std::remove_cvref_t<P>, Plugin>
    PluginGroupBuilder&& add_before(P&& plugin) && {
        add_before_impl(
            type_id<Target>(),
            type_name<Target>(),
            make_entry(std::forward<P>(plugin))
        );
        return std::move(*this);
    }

    template<std::derived_from<Plugin> Target, typename P>
        requires std::derived_from<std::remove_cvref_t<P>, Plugin>
    PluginGroupBuilder& add_after(P&& plugin) & {
        add_after_impl(
            type_id<Target>(),
            type_name<Target>(),
            make_entry(std::forward<P>(plugin))
        );
        return *this;
    }

    template<std::derived_from<Plugin> Target, typename P>
        requires std::derived_from<std::remove_cvref_t<P>, Plugin>
    PluginGroupBuilder&& add_after(P&& plugin) && {
        add_after_impl(
            type_id<Target>(),
            type_name<Target>(),
            make_entry(std::forward<P>(plugin))
        );
        return std::move(*this);
    }

    template<std::derived_from<Plugin> T>
    bool contains() const {
        return find_plugin(type_id<T>()) != m_plugins.end();
    }

    template<std::derived_from<Plugin> T>
    bool enabled() const {
        auto it = find_plugin(type_id<T>());
        return it != m_plugins.end() && it->enabled;
    }

    void finish(App& app);

  private:
    template<typename P>
    static PluginEntry make_entry(P&& plugin) {
        using PluginT = std::remove_cvref_t<P>;
        return PluginEntry {
            .type = type_id<PluginT>(),
            .name = std::string(type_name<PluginT>()),
            .plugin = std::make_unique<PluginT>(std::forward<P>(plugin)),
            .enabled = true,
        };
    }

    std::vector<PluginEntry>::iterator find_plugin(TypeId type);
    std::vector<PluginEntry>::const_iterator find_plugin(TypeId type) const;
    void add_impl(PluginEntry entry);
    void set_impl(PluginEntry entry);
    void enable_impl(TypeId plugin_type, std::string_view plugin_name);
    void disable_impl(TypeId plugin_type, std::string_view plugin_name);
    void add_before_impl(
        TypeId target_type,
        std::string_view target_name,
        PluginEntry entry
    );
    void add_after_impl(
        TypeId target_type,
        std::string_view target_name,
        PluginEntry entry
    );

    std::string m_name;
    std::vector<PluginEntry> m_plugins;
};

class PluginGroup {
  public:
    virtual ~PluginGroup() = default;
    virtual PluginGroupBuilder build() = 0;
};

} // namespace fei
