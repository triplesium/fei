#pragma once

#include "pbr/passes/target.hpp"

#include <array>
#include <memory>

namespace fei::devtools::pbr {

inline constexpr const char* c_render_targets_capability = "pbr.render_targets";
inline constexpr const char* c_composite_capability = "rendering.frame";
inline constexpr const char* c_albedo_metallic_capability =
    "pbr.gbuffer.albedo_metallic";
inline constexpr const char* c_specular_capability = "pbr.gbuffer.specular";

struct RenderTargetDescriptor {
    const char* id;
    const char* label;
    const char* blob_capability;
    const char* capture_name {""};
    PixelFormat expected_format;
    std::shared_ptr<Texture> DeferredViewTargets::* texture;
};

const std::array<RenderTargetDescriptor, 8>& render_target_descriptors();

std::shared_ptr<Texture> resolve_render_target(
    const DeferredViewTargets& targets,
    const RenderTargetDescriptor& descriptor
);

bool is_directly_capturable(const std::shared_ptr<Texture>& texture);

} // namespace fei::devtools::pbr
