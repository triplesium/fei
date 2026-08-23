#include "imgui/frame.hpp"

#include "base/log.hpp"

#include <cstring>
#include <imgui.h>
#include <limits>

namespace ets {

namespace {

constexpr uint64 managed_texture_id_mask = uint64 {1} << 63;
constexpr uint32 managed_texture_bytes_per_pixel = 4;

uint32 checked_u32(std::size_t value, const char* label) {
    if (value > std::numeric_limits<uint32>::max()) {
        fatal("ImGui {} exceeds uint32 range", label);
    }
    return static_cast<uint32>(value);
}

int32 checked_i32(std::size_t value, const char* label) {
    if (value > static_cast<std::size_t>(std::numeric_limits<int32>::max())) {
        fatal("ImGui {} exceeds int32 range", label);
    }
    return static_cast<int32>(value);
}

uint64 managed_texture_id(const ImTextureData& texture_data) {
    static_assert(std::numeric_limits<ImTextureID>::digits >= 64);
    const auto unique_id = static_cast<uint64>(texture_data.UniqueID);
    if ((unique_id & managed_texture_id_mask) != 0) {
        fatal(
            "ImGui managed texture ID {} exceeds the reserved range",
            unique_id
        );
    }
    return managed_texture_id_mask | unique_id;
}

std::vector<std::byte> copy_texture_pixels(
    ImTextureData& texture_data,
    uint32 x,
    uint32 y,
    uint32 width,
    uint32 height
) {
    const auto row_size =
        static_cast<std::size_t>(width) * managed_texture_bytes_per_pixel;
    std::vector<std::byte> pixels(row_size * height);
    const auto* source = static_cast<const std::byte*>(
        texture_data.GetPixelsAt(static_cast<int>(x), static_cast<int>(y))
    );
    const auto source_pitch = static_cast<std::size_t>(texture_data.GetPitch());
    for (uint32 row = 0; row < height; ++row) {
        std::memcpy(
            pixels.data() + row_size * row,
            source + source_pitch * row,
            row_size
        );
    }
    return pixels;
}

void capture_texture_operations(
    ImGuiFrameSnapshot& snapshot,
    ImVector<ImTextureData*>* textures
) {
    if (!textures) {
        return;
    }
    for (ImTextureData* texture_data : *textures) {
        if (!texture_data || texture_data->Status == ImTextureStatus_OK ||
            texture_data->Status == ImTextureStatus_Destroyed) {
            continue;
        }

        if (texture_data->Status == ImTextureStatus_WantDestroy) {
            snapshot.texture_operations.push_back(
                ImGuiTextureOperation {
                    .kind = ImGuiTextureOperationKind::Destroy,
                    .texture_id = static_cast<uint64>(texture_data->GetTexID()),
                }
            );
            texture_data->SetTexID(ImTextureID_Invalid);
            texture_data->SetStatus(ImTextureStatus_Destroyed);
            continue;
        }

        if (texture_data->Format != ImTextureFormat_RGBA32) {
            error(
                "entisium-imgui only supports RGBA32 managed textures (texture "
                "{})",
                texture_data->UniqueID
            );
            continue;
        }

        if (texture_data->Status == ImTextureStatus_WantCreate) {
            if (texture_data->Width <= 0 || texture_data->Height <= 0) {
                error(
                    "entisium-imgui cannot create managed texture {} with size "
                    "{}x{}",
                    texture_data->UniqueID,
                    texture_data->Width,
                    texture_data->Height
                );
                continue;
            }
            const auto width = static_cast<uint32>(texture_data->Width);
            const auto height = static_cast<uint32>(texture_data->Height);
            const auto texture_id = managed_texture_id(*texture_data);
            snapshot.texture_operations.push_back(
                ImGuiTextureOperation {
                    .kind = ImGuiTextureOperationKind::Create,
                    .texture_id = texture_id,
                    .width = width,
                    .height = height,
                    .pixels =
                        copy_texture_pixels(*texture_data, 0, 0, width, height),
                }
            );
            texture_data->SetTexID(static_cast<ImTextureID>(texture_id));
            texture_data->SetStatus(ImTextureStatus_OK);
            continue;
        }

        if (texture_data->Status == ImTextureStatus_WantUpdates) {
            const auto& rect = texture_data->UpdateRect;
            if (rect.w != 0 && rect.h != 0) {
                snapshot.texture_operations.push_back(
                    ImGuiTextureOperation {
                        .kind = ImGuiTextureOperationKind::Update,
                        .texture_id =
                            static_cast<uint64>(texture_data->GetTexID()),
                        .x = rect.x,
                        .y = rect.y,
                        .width = rect.w,
                        .height = rect.h,
                        .pixels = copy_texture_pixels(
                            *texture_data,
                            rect.x,
                            rect.y,
                            rect.w,
                            rect.h
                        ),
                    }
                );
            }
            texture_data->SetStatus(ImTextureStatus_OK);
        }
    }
}

} // namespace

std::shared_ptr<const ImGuiFrameSnapshot> capture_imgui_frame_snapshot() {
    auto snapshot = std::make_shared<ImGuiFrameSnapshot>();
    if (!ImGui::GetCurrentContext()) {
        return snapshot;
    }

    ImGui::Render();
    const ImDrawData* draw_data = ImGui::GetDrawData();
    if (!draw_data) {
        return snapshot;
    }

    capture_texture_operations(*snapshot, draw_data->Textures);
    snapshot->display_position =
        Vector2 {draw_data->DisplayPos.x, draw_data->DisplayPos.y};
    snapshot->display_size =
        Vector2 {draw_data->DisplaySize.x, draw_data->DisplaySize.y};
    snapshot->framebuffer_scale = Vector2 {
        draw_data->FramebufferScale.x,
        draw_data->FramebufferScale.y,
    };
    snapshot->vertices.reserve(
        static_cast<std::size_t>(draw_data->TotalVtxCount)
    );
    snapshot->indices.reserve(
        static_cast<std::size_t>(draw_data->TotalIdxCount)
    );

    std::size_t global_index_offset = 0;
    std::size_t global_vertex_offset = 0;
    for (const ImDrawList* command_list : draw_data->CmdLists) {
        for (const ImDrawVert& vertex : command_list->VtxBuffer) {
            snapshot->vertices.push_back(
                ImGuiFrameVertex {
                    .position = Vector2 {vertex.pos.x, vertex.pos.y},
                    .uv = Vector2 {vertex.uv.x, vertex.uv.y},
                    .color = vertex.col,
                }
            );
        }
        for (const ImDrawIdx index : command_list->IdxBuffer) {
            snapshot->indices.push_back(static_cast<uint32>(index));
        }

        for (const ImDrawCmd& draw_command : command_list->CmdBuffer) {
            if (draw_command.UserCallback) {
                if (draw_command.UserCallback ==
                    ImDrawCallback_ResetRenderState) {
                    snapshot->commands.push_back(
                        ImGuiFrameCommand {
                            .kind = ImGuiFrameCommandKind::ResetRenderState,
                        }
                    );
                } else {
                    error(
                        "entisium-imgui skipped an ImDrawCallback because "
                        "arbitrary "
                        "callbacks cannot cross the Render Worker boundary"
                    );
                }
                continue;
            }
            snapshot->commands.push_back(
                ImGuiFrameCommand {
                    .clip_rect =
                        Vector4 {
                            draw_command.ClipRect.x,
                            draw_command.ClipRect.y,
                            draw_command.ClipRect.z,
                            draw_command.ClipRect.w,
                        },
                    .texture_id = static_cast<uint64>(draw_command.GetTexID()),
                    .element_count = draw_command.ElemCount,
                    .first_index = checked_u32(
                        global_index_offset + draw_command.IdxOffset,
                        "first index"
                    ),
                    .vertex_offset = checked_i32(
                        global_vertex_offset + draw_command.VtxOffset,
                        "vertex offset"
                    ),
                }
            );
        }
        global_index_offset +=
            static_cast<std::size_t>(command_list->IdxBuffer.Size);
        global_vertex_offset +=
            static_cast<std::size_t>(command_list->VtxBuffer.Size);
    }
    return snapshot;
}

} // namespace ets
