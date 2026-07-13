#include "snapshot_types.hpp"

#include "render_targets.hpp"

#include <string>

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

} // namespace

RenderTargetsSnapshot
make_render_targets_snapshot(const DeferredViewTargets& targets) {
    const auto& descriptors = render_target_descriptors();
    RenderTargetsSnapshot snapshot {
        .available = true,
        .total_targets = descriptors.size(),
    };
    snapshot.targets.reserve(descriptors.size());

    for (const auto& descriptor : descriptors) {
        auto texture = resolve_render_target(targets, descriptor);
        const auto capturable = is_directly_capturable(texture) &&
                                descriptor.blob_capability[0] != '\0';
        snapshot.targets.push_back(
            RenderTargetSnapshot {
                .id = descriptor.id,
                .label = descriptor.label,
                .available = texture != nullptr,
                .directly_capturable = capturable,
                .blob_capability = descriptor.blob_capability,
                .format = pixel_format_name(
                    texture ? texture->format() : descriptor.expected_format
                ),
                .width = texture ? texture->width() : 0,
                .height = texture ? texture->height() : 0,
                .depth = texture ? texture->depth() : 0,
                .mip_levels = texture ? texture->mip_level() : 0,
                .layers = texture ? texture->layer() : 0,
            }
        );
        snapshot.available_targets += texture != nullptr ? 1 : 0;
        snapshot.directly_capturable_targets += capturable ? 1 : 0;
    }

    return snapshot;
}

} // namespace fei::devtools::pbr
