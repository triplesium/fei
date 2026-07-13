#include "snapshot_types.hpp"

#include "render_targets.hpp"

#include <string>
#include <utility>

namespace fei::devtools::pbr {

namespace {

std::string pixel_format_name(PixelFormat format) {
    switch (format) {
        case PixelFormat::Rgba8Unorm:
            return "Rgba8Unorm";
        case PixelFormat::Rgba16Float:
            return "Rgba16Float";
        default:
            return "Unknown";
    }
}

Optional<BlobRef> make_blob_ref(bool available, const char* capability) {
    if (!available) {
        return nullopt;
    }
    return BlobRef {.capability = capability};
}

} // namespace

RenderTargetsSnapshot
make_render_targets_snapshot(const DeferredViewTargets& targets) {
    const auto& descriptors = render_target_descriptors();
    RenderTargetsSnapshot snapshot;
    snapshot.available = true;
    snapshot.total_targets = descriptors.size();
    snapshot.targets.reserve(descriptors.size());

    for (const auto& descriptor : descriptors) {
        auto texture = resolve_render_target(targets, descriptor);
        const auto previewable = is_previewable(texture);
        RenderTargetSnapshot target_snapshot {
            .id = descriptor.id,
            .label = descriptor.label,
            .available = texture != nullptr,
            .format = pixel_format_name(
                texture ? texture->format() : descriptor.expected_format
            ),
            .width = texture ? texture->width() : 0,
            .height = texture ? texture->height() : 0,
            .depth = texture ? texture->depth() : 0,
            .mip_levels = texture ? texture->mip_level() : 0,
            .layers = texture ? texture->layer() : 0,
        };
        target_snapshot.views.reserve(descriptor.views.size());
        for (const auto& view : descriptor.views) {
            auto blob_ref = make_blob_ref(previewable, view.blob_capability);
            target_snapshot.views.push_back(
                RenderTargetViewSnapshot {
                    .id = view.id,
                    .label = view.label,
                    .available = previewable,
                    .preview = blob_ref,
                    .visualization = preview_mode_name(view.mode),
                }
            );
            if (blob_ref) {
                snapshot.previews.push_back(std::move(*blob_ref));
            }
            ++snapshot.total_views;
            snapshot.available_views += previewable ? 1 : 0;
        }
        snapshot.targets.push_back(std::move(target_snapshot));
        snapshot.available_targets += texture != nullptr ? 1 : 0;
    }

    return snapshot;
}

} // namespace fei::devtools::pbr
