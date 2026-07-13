#pragma once

#include "pbr/passes/target.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <span>

namespace fei::devtools::pbr {

inline constexpr const char* c_render_targets_capability = "pbr.render_targets";
inline constexpr const char* c_composite_capability = "rendering.frame";
inline constexpr const char* c_position_capability = "pbr.gbuffer.position";
inline constexpr const char* c_normal_capability = "pbr.gbuffer.normal";
inline constexpr const char* c_roughness_capability = "pbr.gbuffer.roughness";
inline constexpr const char* c_albedo_capability = "pbr.gbuffer.albedo";
inline constexpr const char* c_metallic_capability = "pbr.gbuffer.metallic";
inline constexpr const char* c_specular_capability = "pbr.gbuffer.specular";
inline constexpr const char* c_emissive_capability = "pbr.gbuffer.emissive";
inline constexpr const char* c_depth_capability = "pbr.gbuffer.depth";
inline constexpr const char* c_direct_capability = "pbr.lighting.direct";
inline constexpr const char* c_indirect_capability = "pbr.lighting.indirect";

enum class PreviewMode : uint8 {
    Color,
    ScalarAlpha,
    Normal,
    Position,
    Depth,
    ToneMappedColor,
};

const char* preview_mode_name(PreviewMode mode);

struct RenderTargetViewDescriptor {
    const char* id;
    const char* label;
    const char* blob_capability;
    PreviewMode mode;
    bool geometry_mask {true};
};

struct RenderTargetDescriptor {
    const char* id;
    const char* label;
    const char* capture_name;
    PixelFormat expected_format;
    std::shared_ptr<Texture> DeferredViewTargets::* texture;
    std::span<const RenderTargetViewDescriptor> views;
};

struct RenderTargetViewRef {
    const RenderTargetDescriptor* target {nullptr};
    const RenderTargetViewDescriptor* view {nullptr};
};

const std::array<RenderTargetDescriptor, 8>& render_target_descriptors();
std::size_t render_target_view_count();
RenderTargetViewRef render_target_view_at(std::size_t index);

std::shared_ptr<Texture> resolve_render_target(
    const DeferredViewTargets& targets,
    const RenderTargetDescriptor& descriptor
);

bool is_previewable(const std::shared_ptr<Texture>& texture);

} // namespace fei::devtools::pbr
