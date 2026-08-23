#include "snapshot_runtime_asset/adapters.hpp"

#include "asset/database.hpp"
#include "asset/importer.hpp"
#include "asset/request.hpp"
#include "asset/serialization.hpp"
#include "asset/server.hpp"
#include "ecs/world.hpp"
#include "task/plugin.hpp"

#include <string>

namespace ets::snapshot_runtime_asset {
namespace {

snapshot::SnapshotError configuration_error(std::string message) {
    return snapshot::SnapshotError {
        .kind = snapshot::SnapshotError::Kind::InvalidConfiguration,
        .path = "asset",
        .message = std::move(message),
    };
}

template<class T>
void ignore_if_present(World& world, snapshot::SnapshotRegistry& registry) {
    if (world.has_resource<T>()) {
        registry.resource<T>(snapshot::ResourcePolicy::Ignore);
    }
}

} // namespace

Status<snapshot::SnapshotError>
configure_asset_adapters(World& world, snapshot::SnapshotRegistry& registry) {
    if (!world.has_resource<AssetServer>()) {
        return failure(
            configuration_error("Asset snapshot adapter requires AssetsPlugin")
        );
    }

    registry.resource<AssetServer>(snapshot::ResourcePolicy::Ignore);
    ignore_if_present<AssetDatabase>(world, registry);
    ignore_if_present<AssetImporterRegistry>(world, registry);
    ignore_if_present<AssetLoadRequests>(world, registry);
    ignore_if_present<Tasks>(world, registry);

    auto& server = world.resource<AssetServer>();
    for (const auto registration : server.registered_asset_types()) {
        if (!world.has_resource(registration.assets_resource_type) ||
            !world.has_resource(registration.events_resource_type)) {
            return failure(configuration_error(
                "Registered asset type is missing its cache or event resource"
            ));
        }
        registry.set_resource_policy(
            registration.assets_resource_type,
            snapshot::ResourcePolicy::Ignore
        );
        registry.set_resource_policy(
            registration.events_resource_type,
            snapshot::ResourcePolicy::Ignore
        );
        if (registry.codecs().find(registration.handle_type) == nullptr &&
            !register_asset_handle_codec(
                registry.codecs(),
                server,
                registration
            )) {
            return failure(configuration_error(
                "Failed to register an asset handle snapshot codec"
            ));
        }
    }
    return {};
}

} // namespace ets::snapshot_runtime_asset
