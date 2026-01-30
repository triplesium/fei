#pragma once
#include "app/plugin.hpp"
#include "asset/server.hpp"

#include <concepts>

namespace fei {

class AssetsPlugin : public Plugin {
  public:
    virtual void setup(App& app) override;
};

struct NoLoader {};

template<typename Asset, typename Loader = NoLoader>
class AssetPlugin : public Plugin {
  public:
    virtual void setup(App& app) override {
        if constexpr (std::is_same_v<Loader, NoLoader>) {
            app.resource<AssetServer>().add_without_loader<Asset>();
        } else if constexpr (std::derived_from<Loader, AssetLoader<Asset>>) {
            app.resource<AssetServer>().add_without_loader<Asset>();
        } else {
            static_assert(
                std::derived_from<Loader, AssetLoader<Asset>>,
                "Loader must derive from AssetLoader<Asset>"
            );
        }
    }
};

} // namespace fei
