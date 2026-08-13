#pragma once
#include "app/plugin.hpp"
#include "asset/server.hpp"
#include "task/plugin.hpp"

#include <concepts>
#include <filesystem>
#include <utility>

namespace fei {

struct AssetsPluginConfig {
    std::filesystem::path project_asset_root;
    std::filesystem::path import_cache_root;
};

FEI_REFLECT(Plugin)
class AssetsPlugin : public Plugin {
  private:
    AssetsPluginConfig m_config;

  public:
    AssetsPlugin() = default;
    explicit AssetsPlugin(AssetsPluginConfig config) :
        m_config(std::move(config)) {}

    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<TaskPlugin>();
    }
    void setup(App& app) override;
};

struct NoLoader {};

template<typename Asset, typename Loader = NoLoader>
class AssetPlugin : public Plugin {
  public:
    void dependencies(PluginDependencies& dependencies) const override {
        dependencies.require<AssetsPlugin>();
    }

    void setup(App& app) override {
        if constexpr (std::is_same_v<Loader, NoLoader>) {
            app.resource<AssetServer>().add_without_loader<Asset>();
        } else if constexpr (std::derived_from<Loader, AssetLoader<Asset>>) {
            app.resource<AssetServer>().add_loader<Asset, Loader>();
        } else {
            static_assert(
                std::derived_from<Loader, AssetLoader<Asset>>,
                "Loader must derive from AssetLoader<Asset>"
            );
        }
    }
};

} // namespace fei
