#include "snapshot_runtime_rendering/adapters.hpp"

#include "graphics/backend.hpp"
#include "rendering/render_app.hpp"

namespace fei::snapshot_runtime_rendering {

Status<snapshot::SnapshotError>
configure_rendering_adapters(World&, snapshot::SnapshotRegistry& registry) {
    registry.component<RenderEntity>(snapshot::ComponentPolicy::Rebuild);
    registry.component<SyncToRenderWorld>(snapshot::ComponentPolicy::Rebuild);

    registry.resource<GraphicsBackendBootstrap>(
        snapshot::ResourcePolicy::Ignore
    );
    registry.resource<GraphicsBackendCapabilities>(
        snapshot::ResourcePolicy::Ignore
    );
    registry.resource<GraphicsSurfaceSize>(snapshot::ResourcePolicy::Ignore);
    return {};
}

} // namespace fei::snapshot_runtime_rendering
