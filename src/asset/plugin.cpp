#include "asset/plugin.hpp"

namespace fei {

void AssetsPlugin::setup(App& app) {
    AssetServer server {&app};
    server.emplace_source<DefaultAssetSource>();
    server.emplace_source<EmbededAssetSource>();
    app.add_resource(std::move(server));
}

} // namespace fei
