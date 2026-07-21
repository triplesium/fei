#include "imgui/renderer.hpp"

#include "base/log.hpp"
#include "graphics/buffer.hpp"
#include "graphics/command_buffer.hpp"
#include "graphics/enums.hpp"
#include "graphics/framebuffer.hpp"
#include "graphics/graphics_device.hpp"
#include "graphics/pipeline.hpp"
#include "graphics/render_pass.hpp"
#include "graphics/resource.hpp"
#include "graphics/sampler.hpp"
#include "graphics/swapchain.hpp"
#include "graphics/texture.hpp"
#include "math/vector.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/render_frame.hpp"
#include "rendering/shader_cache.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fei {

namespace {

constexpr std::size_t min_imgui_buffer_capacity =
    std::size_t {64} * std::size_t {1024};

static_assert(
    sizeof(ImDrawIdx) == sizeof(uint16) || sizeof(ImDrawIdx) == sizeof(uint32)
);
constexpr IndexFormat imgui_index_format = sizeof(ImDrawIdx) == sizeof(uint16) ?
                                               IndexFormat::Uint16 :
                                               IndexFormat::Uint32;

struct ImGuiFrameUniform {
    Vector2 scale;
    Vector2 translate;
};

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

void upload_texture_region(
    const GraphicsDevice& device,
    const std::shared_ptr<Texture>& texture,
    const std::byte* source,
    std::size_t source_pitch,
    uint32 x,
    uint32 y,
    uint32 width,
    uint32 height,
    uint32 bytes_per_pixel,
    std::vector<std::byte>& scratch
) {
    const auto row_size = static_cast<std::size_t>(width) * bytes_per_pixel;
    const void* upload_data = source;
    if (source_pitch != row_size) {
        scratch.resize(row_size * height);
        for (uint32 row = 0; row < height; ++row) {
            std::memcpy(
                scratch.data() + row_size * row,
                source + source_pitch * row,
                row_size
            );
        }
        upload_data = scratch.data();
    }

    device
        .update_texture(texture, upload_data, x, y, 0, width, height, 1, 0, 0);
}

} // namespace

Optional<ImGuiScissor> calculate_imgui_scissor(
    const ImVec4& clip_rect,
    const ImVec2& display_pos,
    const ImVec2& framebuffer_scale,
    uint32 framebuffer_width,
    uint32 framebuffer_height
) {
    float min_x = (clip_rect.x - display_pos.x) * framebuffer_scale.x;
    float min_y = (clip_rect.y - display_pos.y) * framebuffer_scale.y;
    float max_x = (clip_rect.z - display_pos.x) * framebuffer_scale.x;
    float max_y = (clip_rect.w - display_pos.y) * framebuffer_scale.y;

    min_x = std::clamp(min_x, 0.0f, static_cast<float>(framebuffer_width));
    min_y = std::clamp(min_y, 0.0f, static_cast<float>(framebuffer_height));
    max_x = std::clamp(max_x, 0.0f, static_cast<float>(framebuffer_width));
    max_y = std::clamp(max_y, 0.0f, static_cast<float>(framebuffer_height));
    if (max_x <= min_x || max_y <= min_y) {
        return {};
    }

    const auto left = static_cast<uint32>(min_x);
    const auto top = static_cast<uint32>(min_y);
    const auto right = static_cast<uint32>(max_x);
    const auto bottom = static_cast<uint32>(max_y);
    if (right <= left || bottom <= top) {
        return {};
    }
    return ImGuiScissor {
        .x = checked_i32(left, "scissor x"),
        .y = checked_i32(top, "scissor y"),
        .width = right - left,
        .height = bottom - top,
    };
}

std::size_t imgui_buffer_capacity(std::size_t required_size) {
    const auto requested = std::max(required_size, min_imgui_buffer_capacity);
    if (requested > std::bit_floor(std::numeric_limits<std::size_t>::max())) {
        fatal("ImGui buffer size is too large");
    }
    return std::bit_ceil(requested);
}

std::size_t imgui_frame_slot(std::size_t frame_index, std::size_t slot_count) {
    if (slot_count == 0) {
        fatal("ImGui frame ring requires at least one slot");
    }
    return frame_index % slot_count;
}

ImGuiDrawOffsets calculate_imgui_draw_offsets(
    std::size_t global_index_offset,
    std::size_t global_vertex_offset,
    const ImDrawCmd& draw_command
) {
    return ImGuiDrawOffsets {
        .first_index = checked_u32(
            global_index_offset + draw_command.IdxOffset,
            "first index"
        ),
        .vertex_offset = checked_i32(
            global_vertex_offset + draw_command.VtxOffset,
            "vertex offset"
        ),
    };
}

struct ImGuiTextureRegistry::Impl {
    struct Entry {
        std::shared_ptr<const Texture> texture;
        std::shared_ptr<const Sampler> sampler;
        std::shared_ptr<const ResourceSet> resource_set;
        bool pending_removal {false};
    };

    const GraphicsDevice* device {nullptr};
    std::shared_ptr<const ResourceLayout> texture_layout;
    std::shared_ptr<const Sampler> default_sampler;
    std::unordered_map<ImTextureID, Entry> entries;
    ImTextureID next_id {1};
};

ImGuiTextureRegistry::ImGuiTextureRegistry() :
    m_impl(std::make_unique<Impl>()) {}
ImGuiTextureRegistry::~ImGuiTextureRegistry() = default;
ImGuiTextureRegistry::ImGuiTextureRegistry(ImGuiTextureRegistry&&) noexcept =
    default;
ImGuiTextureRegistry&
ImGuiTextureRegistry::operator=(ImGuiTextureRegistry&&) noexcept = default;

void ImGuiTextureRegistry::initialize(
    const GraphicsDevice& device,
    std::shared_ptr<const ResourceLayout> texture_layout,
    std::shared_ptr<const Sampler> default_sampler
) {
    m_impl->device = &device;
    m_impl->texture_layout = std::move(texture_layout);
    m_impl->default_sampler = std::move(default_sampler);
}

ImTextureID ImGuiTextureRegistry::register_texture(
    std::shared_ptr<const Texture> texture,
    std::shared_ptr<const Sampler> sampler
) {
    if (!m_impl->device || !m_impl->texture_layout ||
        !m_impl->default_sampler) {
        fatal("ImGuiTextureRegistry is not initialized");
    }
    if (!texture) {
        fatal("ImGuiTextureRegistry cannot register a null texture");
    }
    if (!sampler) {
        sampler = m_impl->default_sampler;
    }
    if (m_impl->next_id == ImTextureID_Invalid ||
        m_impl->next_id == std::numeric_limits<ImTextureID>::max()) {
        fatal("ImGuiTextureRegistry exhausted texture IDs");
    }

    const ImTextureID texture_id = m_impl->next_id++;
    auto resource_set = m_impl->device->create_resource_set(
        ResourceSetDescription {
            .layout = m_impl->texture_layout,
            .resources = {texture, sampler},
            .name = "imgui_texture",
        }
    );
    m_impl->entries.emplace(
        texture_id,
        Impl::Entry {
            .texture = std::move(texture),
            .sampler = std::move(sampler),
            .resource_set = std::move(resource_set),
        }
    );
    return texture_id;
}

void ImGuiTextureRegistry::unregister_texture(ImTextureID texture_id) {
    if (auto entry = m_impl->entries.find(texture_id);
        entry != m_impl->entries.end()) {
        entry->second.pending_removal = true;
    }
}

bool ImGuiTextureRegistry::contains(ImTextureID texture_id) const {
    return m_impl->entries.contains(texture_id);
}

bool ImGuiTextureRegistry::pending_removal(ImTextureID texture_id) const {
    const auto entry = m_impl->entries.find(texture_id);
    return entry != m_impl->entries.end() && entry->second.pending_removal;
}

std::size_t ImGuiTextureRegistry::size() const {
    return m_impl->entries.size();
}

std::shared_ptr<const ResourceSet>
ImGuiTextureRegistry::resource_set(ImTextureID texture_id) const {
    const auto entry = m_impl->entries.find(texture_id);
    return entry == m_impl->entries.end() ? nullptr :
                                            entry->second.resource_set;
}

void ImGuiTextureRegistry::end_frame() {
    std::erase_if(m_impl->entries, [](const auto& item) {
        return item.second.pending_removal;
    });
}

void ImGuiTextureRegistry::clear() noexcept {
    m_impl->entries.clear();
    m_impl->device = nullptr;
    m_impl->texture_layout.reset();
    m_impl->default_sampler.reset();
}

struct ImGuiRenderer::Impl {
    struct FrameSlot {
        std::shared_ptr<Buffer> vertex_buffer;
        std::shared_ptr<Buffer> index_buffer;
        std::shared_ptr<Buffer> uniform_buffer;
        std::shared_ptr<ResourceSet> frame_resource_set;
        std::size_t vertex_capacity {0};
        std::size_t index_capacity {0};
    };

    std::shared_ptr<ResourceLayout> frame_layout;
    std::shared_ptr<ResourceLayout> texture_layout;
    std::shared_ptr<Sampler> default_sampler;
    std::vector<FrameSlot> slots;
    std::size_t next_slot {0};
    std::vector<ImDrawVert> vertices;
    std::vector<ImDrawIdx> indices;
    std::vector<std::byte> texture_scratch;
    std::unordered_map<ImTextureID, std::shared_ptr<Texture>> managed_textures;
    std::unordered_set<ImTextureID> missing_texture_errors;
    std::optional<PixelFormat> pipeline_format;
    std::optional<CachedRenderPipelineId> pipeline_id;
    bool initialized {false};
};

ImGuiRenderer::ImGuiRenderer() : m_impl(std::make_unique<Impl>()) {}
ImGuiRenderer::~ImGuiRenderer() = default;
ImGuiRenderer::ImGuiRenderer(ImGuiRenderer&&) noexcept = default;
ImGuiRenderer& ImGuiRenderer::operator=(ImGuiRenderer&&) noexcept = default;

void ImGuiRenderer::initialize(
    const GraphicsDevice& device,
    ImGuiTextureRegistry& texture_registry
) {
    if (m_impl->initialized) {
        return;
    }
    m_impl->frame_layout = device.create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Vertex},
            {uniform_buffer("frame")}
        )
    );
    m_impl->texture_layout = device.create_resource_layout(
        ResourceLayoutDescription::sequencial(
            {ShaderStages::Fragment},
            {texture_read_only("image"), sampler("image_sampler")}
        )
    );
    auto sampler_description = SamplerDescription::Linear;
    sampler_description.address_mode_u = SamplerAddressMode::ClampToEdge;
    sampler_description.address_mode_v = SamplerAddressMode::ClampToEdge;
    sampler_description.address_mode_w = SamplerAddressMode::ClampToEdge;
    m_impl->default_sampler = device.create_sampler(sampler_description);
    texture_registry
        .initialize(device, m_impl->texture_layout, m_impl->default_sampler);

    const auto slot_count =
        std::max<std::size_t>(1, device.max_frames_in_flight());
    m_impl->slots.resize(slot_count);
    for (auto& slot : m_impl->slots) {
        slot.vertex_capacity = min_imgui_buffer_capacity;
        slot.index_capacity = min_imgui_buffer_capacity;
        slot.vertex_buffer = device.create_buffer(
            BufferDescription {
                .size = slot.vertex_capacity,
                .usages = {BufferUsages::Vertex, BufferUsages::Dynamic},
            }
        );
        slot.index_buffer = device.create_buffer(
            BufferDescription {
                .size = slot.index_capacity,
                .usages = {BufferUsages::Index, BufferUsages::Dynamic},
            }
        );
        slot.uniform_buffer = device.create_buffer(
            BufferDescription {
                .size = sizeof(ImGuiFrameUniform),
                .usages = {BufferUsages::Uniform, BufferUsages::Dynamic},
            }
        );
        slot.frame_resource_set = device.create_resource_set(
            ResourceSetDescription {
                .layout = m_impl->frame_layout,
                .resources = {slot.uniform_buffer},
                .name = "imgui_frame",
            }
        );
    }
    m_impl->initialized = true;
}

void ImGuiRenderer::prepare_pipeline(
    ShaderCache& shader_cache,
    PipelineCache& pipeline_cache,
    const Swapchain& swapchain
) {
    if (!m_impl->initialized) {
        return;
    }
    auto framebuffer = swapchain.framebuffer();
    if (!framebuffer) {
        return;
    }
    const auto format = swapchain.color_format();
    if (m_impl->pipeline_id && m_impl->pipeline_format == format) {
        return;
    }

    auto vertex_shader = shader_cache.get_or_compile(
        AssetPath("shader://imgui/imgui.slang"),
        ShaderStages::Vertex,
        "vertex_main"
    );
    auto fragment_shader = shader_cache.get_or_compile(
        AssetPath("shader://imgui/imgui.slang"),
        ShaderStages::Fragment,
        "fragment_main"
    );
    BlendAttachmentDescription blend {
        .enabled = true,
        .color_write_mask = ColorWriteMask::All,
        .source_color_factor = BlendFactor::SrcAlpha,
        .destination_color_factor = BlendFactor::OneMinusSrcAlpha,
        .color_function = BlendFunction::Add,
        .source_alpha_factor = BlendFactor::One,
        .destination_alpha_factor = BlendFactor::OneMinusSrcAlpha,
        .alpha_function = BlendFunction::Add,
    };
    m_impl->pipeline_id = pipeline_cache.request_render_pipeline(
        RenderPipelineDescription {
            .blend_state = BlendStateDescription {{blend}},
            .depth_stencil_state = DepthStencilStateDescription::Disabled,
            .rasterizer_state =
                RasterizerStateDescription {
                    .cull_mode = CullMode::None,
                    .scissor_test_enabled = true,
                },
            .render_primitive = RenderPrimitive::Triangles,
            .shader_program =
                ShaderProgramDescription {
                    .vertex_layouts =
                        {
                            VertexLayoutDescription {
                                .attributes =
                                    {
                                        VertexAttributeDescription {
                                            .location = 0,
                                            .offset = offsetof(ImDrawVert, pos),
                                            .format = VertexFormat::Float2,
                                        },
                                        VertexAttributeDescription {
                                            .location = 1,
                                            .offset = offsetof(ImDrawVert, uv),
                                            .format = VertexFormat::Float2,
                                        },
                                        VertexAttributeDescription {
                                            .location = 2,
                                            .offset = offsetof(ImDrawVert, col),
                                            .format = VertexFormat::UByte4,
                                            .normalized = true,
                                        },
                                    },
                                .stride = sizeof(ImDrawVert),
                            },
                        },
                    .shaders = {vertex_shader, fragment_shader},
                },
            .resource_layouts = {m_impl->frame_layout, m_impl->texture_layout},
            .output_description = framebuffer->output_description(),
        }
    );
    m_impl->pipeline_format = format;
}

namespace {

void process_managed_textures(
    ImGuiRenderer::Impl& renderer,
    const GraphicsDevice& device,
    ImGuiTextureRegistry& registry,
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
            const auto texture_id = texture_data->GetTexID();
            registry.unregister_texture(texture_id);
            renderer.managed_textures.erase(texture_id);
            texture_data->SetTexID(ImTextureID_Invalid);
            texture_data->SetStatus(ImTextureStatus_Destroyed);
            continue;
        }
        if (texture_data->Format != ImTextureFormat_RGBA32) {
            error(
                "fei-imgui only supports RGBA32 managed textures (texture {})",
                texture_data->UniqueID
            );
            continue;
        }

        if (texture_data->Status == ImTextureStatus_WantCreate) {
            if (texture_data->Width <= 0 || texture_data->Height <= 0) {
                error(
                    "fei-imgui cannot create managed texture {} with size "
                    "{}x{}",
                    texture_data->UniqueID,
                    texture_data->Width,
                    texture_data->Height
                );
                continue;
            }
            auto texture = device.create_texture(
                TextureDescription {
                    .width = static_cast<uint32>(texture_data->Width),
                    .height = static_cast<uint32>(texture_data->Height),
                    .depth = 1,
                    .mip_level = 1,
                    .layer = 1,
                    .texture_format = PixelFormat::Rgba8Unorm,
                    .texture_usage = TextureUsage::Sampled,
                    .texture_type = TextureType::Texture2D,
                }
            );
            upload_texture_region(
                device,
                texture,
                static_cast<const std::byte*>(texture_data->GetPixels()),
                static_cast<std::size_t>(texture_data->GetPitch()),
                0,
                0,
                static_cast<uint32>(texture_data->Width),
                static_cast<uint32>(texture_data->Height),
                4,
                renderer.texture_scratch
            );
            const auto texture_id = registry.register_texture(texture);
            renderer.managed_textures.emplace(texture_id, std::move(texture));
            texture_data->SetTexID(texture_id);
            texture_data->SetStatus(ImTextureStatus_OK);
            continue;
        }

        if (texture_data->Status == ImTextureStatus_WantUpdates) {
            const auto texture =
                renderer.managed_textures.find(texture_data->GetTexID());
            if (texture == renderer.managed_textures.end()) {
                error(
                    "fei-imgui cannot update unknown managed texture {}",
                    texture_data->UniqueID
                );
                continue;
            }
            const auto& rect = texture_data->UpdateRect;
            if (rect.w != 0 && rect.h != 0) {
                upload_texture_region(
                    device,
                    texture->second,
                    static_cast<const std::byte*>(
                        texture_data->GetPixelsAt(rect.x, rect.y)
                    ),
                    static_cast<std::size_t>(texture_data->GetPitch()),
                    rect.x,
                    rect.y,
                    rect.w,
                    rect.h,
                    4,
                    renderer.texture_scratch
                );
            }
            texture_data->SetStatus(ImTextureStatus_OK);
            continue;
        }
    }
}

} // namespace

void ImGuiRenderer::render(
    const GraphicsDevice& device,
    PipelineCache& pipeline_cache,
    RenderFrameContext& frame_context,
    const MainSwapchain& main_swapchain,
    ImGuiTextureRegistry& texture_registry
) {
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (!draw_data) {
        texture_registry.end_frame();
        return;
    }
    process_managed_textures(
        *m_impl,
        device,
        texture_registry,
        draw_data->Textures
    );

    const auto finish_overlay = [&texture_registry]() {
        texture_registry.end_frame();
    };
    if (!main_swapchain.swapchain || !frame_context.recording() ||
        !m_impl->pipeline_id) {
        finish_overlay();
        return;
    }
    auto framebuffer = main_swapchain.swapchain->framebuffer();
    if (!framebuffer) {
        finish_overlay();
        return;
    }
    auto pipeline = pipeline_cache.get_render_pipeline(*m_impl->pipeline_id);
    const auto framebuffer_width = static_cast<int>(
        draw_data->DisplaySize.x * draw_data->FramebufferScale.x
    );
    const auto framebuffer_height = static_cast<int>(
        draw_data->DisplaySize.y * draw_data->FramebufferScale.y
    );
    if (!pipeline || framebuffer_width <= 0 || framebuffer_height <= 0 ||
        draw_data->TotalVtxCount <= 0 || draw_data->TotalIdxCount <= 0) {
        finish_overlay();
        return;
    }

    m_impl->vertices.clear();
    m_impl->indices.clear();
    m_impl->vertices.reserve(
        static_cast<std::size_t>(draw_data->TotalVtxCount)
    );
    m_impl->indices.reserve(static_cast<std::size_t>(draw_data->TotalIdxCount));
    for (const ImDrawList* command_list : draw_data->CmdLists) {
        m_impl->vertices.insert(
            m_impl->vertices.end(),
            command_list->VtxBuffer.begin(),
            command_list->VtxBuffer.end()
        );
        m_impl->indices.insert(
            m_impl->indices.end(),
            command_list->IdxBuffer.begin(),
            command_list->IdxBuffer.end()
        );
    }

    auto& slot = m_impl->slots[m_impl->next_slot];
    const auto vertex_size = m_impl->vertices.size() * sizeof(ImDrawVert);
    const auto index_size = m_impl->indices.size() * sizeof(ImDrawIdx);
    if (vertex_size > slot.vertex_capacity) {
        slot.vertex_capacity = imgui_buffer_capacity(vertex_size);
        slot.vertex_buffer = device.create_buffer(
            BufferDescription {
                .size = slot.vertex_capacity,
                .usages = {BufferUsages::Vertex, BufferUsages::Dynamic},
            }
        );
    }
    if (index_size > slot.index_capacity) {
        slot.index_capacity = imgui_buffer_capacity(index_size);
        slot.index_buffer = device.create_buffer(
            BufferDescription {
                .size = slot.index_capacity,
                .usages = {BufferUsages::Index, BufferUsages::Dynamic},
            }
        );
    }

    ImGuiFrameUniform uniform {
        .scale = Vector2 {
            2.0f / draw_data->DisplaySize.x,
            -2.0f / draw_data->DisplaySize.y,
        },
    };
    uniform.translate = Vector2 {
        -1.0f - draw_data->DisplayPos.x * uniform.scale.x,
        1.0f - draw_data->DisplayPos.y * uniform.scale.y,
    };

    auto* commands = frame_context.command_buffer();
    commands->update_buffer(
        slot.vertex_buffer,
        m_impl->vertices.data(),
        vertex_size
    );
    commands
        ->update_buffer(slot.index_buffer, m_impl->indices.data(), index_size);
    commands->update_buffer(slot.uniform_buffer, &uniform, sizeof(uniform));

    commands->begin_render_pass(
        RenderPassDescription {
            .color_attachments =
                {
                    RenderPassColorAttachment {
                        .load_op = LoadOp::Load,
                        .store_op = StoreOp::Store,
                    },
                },
            .framebuffer = framebuffer,
        }
    );

    const auto bind_render_state = [&]() {
        commands->set_viewport(
            0,
            0,
            static_cast<uint32>(framebuffer_width),
            static_cast<uint32>(framebuffer_height)
        );
        commands->set_render_pipeline(pipeline);
        commands->set_vertex_buffer(slot.vertex_buffer);
        commands->set_index_buffer(slot.index_buffer, imgui_index_format);
        commands->set_resource_set(0, slot.frame_resource_set);
    };
    bind_render_state();

    std::size_t global_index_offset = 0;
    std::size_t global_vertex_offset = 0;
    for (const ImDrawList* command_list : draw_data->CmdLists) {
        for (const ImDrawCmd& draw_command : command_list->CmdBuffer) {
            if (draw_command.UserCallback) {
                if (draw_command.UserCallback ==
                    ImDrawCallback_ResetRenderState) {
                    bind_render_state();
                } else {
                    draw_command.UserCallback(command_list, &draw_command);
                }
                continue;
            }
            const auto scissor = calculate_imgui_scissor(
                draw_command.ClipRect,
                draw_data->DisplayPos,
                draw_data->FramebufferScale,
                static_cast<uint32>(framebuffer_width),
                static_cast<uint32>(framebuffer_height)
            );
            if (!scissor) {
                continue;
            }

            const auto texture_id = draw_command.GetTexID();
            auto texture_set = texture_registry.resource_set(texture_id);
            if (!texture_set) {
                if (m_impl->missing_texture_errors.insert(texture_id).second) {
                    error(
                        "fei-imgui draw references unregistered texture ID {}",
                        static_cast<unsigned long long>(texture_id)
                    );
                }
                continue;
            }
            commands->set_scissor(
                scissor->x,
                scissor->y,
                scissor->width,
                scissor->height
            );
            commands->set_resource_set(1, std::move(texture_set));
            const auto offsets = calculate_imgui_draw_offsets(
                global_index_offset,
                global_vertex_offset,
                draw_command
            );
            commands->draw_indexed(
                draw_command.ElemCount,
                offsets.first_index,
                offsets.vertex_offset
            );
        }
        global_index_offset +=
            static_cast<std::size_t>(command_list->IdxBuffer.Size);
        global_vertex_offset +=
            static_cast<std::size_t>(command_list->VtxBuffer.Size);
    }
    commands->end_render_pass();
    m_impl->next_slot = (m_impl->next_slot + 1) % m_impl->slots.size();
    finish_overlay();
}

void ImGuiRenderer::shutdown(ImGuiTextureRegistry& texture_registry) noexcept {
    if (ImGui::GetCurrentContext()) {
        auto& textures = ImGui::GetPlatformIO().Textures;
        for (ImTextureData* texture_data : textures) {
            if (!texture_data) {
                continue;
            }
            texture_data->SetTexID(ImTextureID_Invalid);
            texture_data->SetStatus(ImTextureStatus_Destroyed);
        }
    }
    m_impl->managed_textures.clear();
    m_impl->missing_texture_errors.clear();
    m_impl->vertices.clear();
    m_impl->indices.clear();
    m_impl->texture_scratch.clear();
    m_impl->slots.clear();
    m_impl->frame_layout.reset();
    m_impl->texture_layout.reset();
    m_impl->default_sampler.reset();
    m_impl->pipeline_id.reset();
    m_impl->pipeline_format.reset();
    m_impl->initialized = false;
    texture_registry.clear();
}

std::size_t ImGuiRenderer::frame_slot_count() const {
    return m_impl->slots.size();
}

std::size_t ImGuiRenderer::frame_slot_index() const {
    return m_impl->next_slot;
}

std::size_t ImGuiRenderer::vertex_capacity(std::size_t slot) const {
    return m_impl->slots.at(slot).vertex_capacity;
}

std::size_t ImGuiRenderer::index_capacity(std::size_t slot) const {
    return m_impl->slots.at(slot).index_capacity;
}

} // namespace fei
