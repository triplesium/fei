#pragma once

#include "base/types.hpp"
#include "math/vector.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace fei {

struct ImGuiFrameVertex {
    Vector2 position;
    Vector2 uv;
    uint32 color {0};
};

enum class ImGuiFrameCommandKind : uint8 {
    Draw,
    ResetRenderState,
};

struct ImGuiFrameCommand {
    ImGuiFrameCommandKind kind {ImGuiFrameCommandKind::Draw};
    Vector4 clip_rect;
    uint64 texture_id {0};
    uint32 element_count {0};
    uint32 first_index {0};
    int32 vertex_offset {0};
};

enum class ImGuiTextureOperationKind : uint8 {
    Create,
    Update,
    Destroy,
};

struct ImGuiTextureOperation {
    ImGuiTextureOperationKind kind {ImGuiTextureOperationKind::Create};
    uint64 texture_id {0};
    uint32 x {0};
    uint32 y {0};
    uint32 width {0};
    uint32 height {0};
    std::vector<std::byte> pixels;
};

struct ImGuiFrameSnapshot {
    Vector2 display_position;
    Vector2 display_size;
    Vector2 framebuffer_scale {1.0f, 1.0f};
    std::vector<ImGuiFrameVertex> vertices;
    std::vector<uint32> indices;
    std::vector<ImGuiFrameCommand> commands;
    std::vector<ImGuiTextureOperation> texture_operations;
};

struct PendingImGuiFrame {
    std::shared_ptr<const ImGuiFrameSnapshot> snapshot;
};

struct ExtractedImGuiFrame {
    std::shared_ptr<const ImGuiFrameSnapshot> snapshot;
};

[[nodiscard]] std::shared_ptr<const ImGuiFrameSnapshot>
capture_imgui_frame_snapshot();

} // namespace fei
