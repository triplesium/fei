#include "snapshot_runtime_rendering/adapters.hpp"

#include "ecs/world.hpp"
#include "graphics/backend.hpp"
#include "rendering/render_app.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace fei;

TEST_CASE(
    "Rendering snapshot adapters classify host and extraction state",
    "[snapshot][rendering][policy]"
) {
    World world;
    snapshot::SnapshotRegistry registry;
    REQUIRE(
        snapshot_runtime_rendering::configure_rendering_adapters(
            world,
            registry
        )
    );

    CHECK(
        registry.component_policy(type_id<RenderEntity>()) ==
        snapshot::ComponentPolicy::Rebuild
    );
    CHECK(
        registry.component_policy(type_id<SyncToRenderWorld>()) ==
        snapshot::ComponentPolicy::Rebuild
    );
    const auto bootstrap =
        registry.resource_policy(type_id<GraphicsBackendBootstrap>());
    const auto capabilities =
        registry.resource_policy(type_id<GraphicsBackendCapabilities>());
    const auto surface_size =
        registry.resource_policy(type_id<GraphicsSurfaceSize>());
    REQUIRE(bootstrap);
    REQUIRE(capabilities);
    REQUIRE(surface_size);
    CHECK(*bootstrap == snapshot::ResourcePolicy::Ignore);
    CHECK(*capabilities == snapshot::ResourcePolicy::Ignore);
    CHECK(*surface_size == snapshot::ResourcePolicy::Ignore);
}
