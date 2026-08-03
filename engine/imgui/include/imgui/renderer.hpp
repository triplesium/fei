#pragma once

#include "base/optional.hpp"
#include "base/types.hpp"

#include <cstddef>
#include <imgui.h>
#include <memory>

namespace fei {

class GraphicsDevice;
class ImGuiRenderer;
class PipelineCache;
class RenderFrameContext;
class ResourceLayout;
class ResourceSet;
class Sampler;
class ShaderCache;
class Swapchain;
class Texture;
struct MainSwapchain;

struct ImGuiScissor {
    int32 x {0};
    int32 y {0};
    uint32 width {0};
    uint32 height {0};

    bool operator==(const ImGuiScissor&) const = default;
};

struct ImGuiDrawOffsets {
    uint32 first_index {0};
    int32 vertex_offset {0};

    bool operator==(const ImGuiDrawOffsets&) const = default;
};

[[nodiscard]] Optional<ImGuiScissor> calculate_imgui_scissor(
    const ImVec4& clip_rect,
    const ImVec2& display_pos,
    const ImVec2& framebuffer_scale,
    uint32 framebuffer_width,
    uint32 framebuffer_height
);

[[nodiscard]] std::size_t imgui_buffer_capacity(std::size_t required_size);
[[nodiscard]] std::size_t
imgui_frame_slot(std::size_t frame_index, std::size_t slot_count);
[[nodiscard]] ImGuiDrawOffsets calculate_imgui_draw_offsets(
    std::size_t global_index_offset,
    std::size_t global_vertex_offset,
    const ImDrawCmd& draw_command
);

class ImGuiTextureRegistry {
  public:
    ImGuiTextureRegistry();
    ~ImGuiTextureRegistry();
    ImGuiTextureRegistry(ImGuiTextureRegistry&&) noexcept;
    ImGuiTextureRegistry& operator=(ImGuiTextureRegistry&&) noexcept;
    ImGuiTextureRegistry(const ImGuiTextureRegistry&) = delete;
    ImGuiTextureRegistry& operator=(const ImGuiTextureRegistry&) = delete;

    ImTextureID register_texture(
        std::shared_ptr<const Texture> texture,
        std::shared_ptr<const Sampler> sampler = nullptr
    );
    void unregister_texture(ImTextureID texture_id);

    [[nodiscard]] bool contains(ImTextureID texture_id) const;
    [[nodiscard]] bool pending_removal(ImTextureID texture_id) const;
    [[nodiscard]] std::size_t size() const;

  private:
    friend class ImGuiRenderer;
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    void initialize(
        const GraphicsDevice& device,
        std::shared_ptr<const ResourceLayout> texture_layout,
        std::shared_ptr<const Sampler> default_sampler
    );
    [[nodiscard]] std::shared_ptr<const ResourceSet>
    resource_set(ImTextureID texture_id) const;
    void end_frame();
    void clear() noexcept;
};

class ImGuiRenderer {
  public:
    struct Impl;

    ImGuiRenderer();
    ~ImGuiRenderer();
    ImGuiRenderer(ImGuiRenderer&&) noexcept;
    ImGuiRenderer& operator=(ImGuiRenderer&&) noexcept;
    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

    void initialize(
        const GraphicsDevice& device,
        ImGuiTextureRegistry& texture_registry
    );
    void prepare_pipeline(
        ShaderCache& shader_cache,
        PipelineCache& pipeline_cache,
        const Swapchain& swapchain
    );
    void render(
        const GraphicsDevice& device,
        PipelineCache& pipeline_cache,
        RenderFrameContext& frame_context,
        const MainSwapchain& main_swapchain,
        ImGuiTextureRegistry& texture_registry
    );
    void shutdown(ImGuiTextureRegistry& texture_registry) noexcept;

    [[nodiscard]] std::size_t frame_slot_count() const;
    [[nodiscard]] std::size_t frame_slot_index() const;
    [[nodiscard]] std::size_t vertex_capacity(std::size_t slot) const;
    [[nodiscard]] std::size_t index_capacity(std::size_t slot) const;

  private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace fei
