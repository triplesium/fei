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
#include "imgui/texture.hpp"
#include "math/vector.hpp"
#include "rendering/defaults.hpp"
#include "rendering/gpu_image.hpp"
#include "rendering/pipeline_cache.hpp"
#include "rendering/render_asset.hpp"
#include "rendering/render_frame.hpp"
#include "rendering/shader_cache.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ets {

namespace {

constexpr std::size_t min_imgui_buffer_capacity =
    std::size_t {64} * std::size_t {1024};

constexpr IndexFormat imgui_index_format = IndexFormat::Uint32;

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

} // namespace

Optional<ImGuiScissor> calculate_imgui_scissor(
    const Vector4& clip_rect,
    const Vector2& display_pos,
    const Vector2& framebuffer_scale,
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
    std::unordered_map<uint64, Entry> entries;
    std::unordered_set<uint64> image_ids;
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
        static_cast<uint64>(texture_id),
        Impl::Entry {
            .texture = std::move(texture),
            .sampler = std::move(sampler),
            .resource_set = std::move(resource_set),
        }
    );
    return texture_id;
}

void ImGuiTextureRegistry::unregister_texture(ImTextureID texture_id) {
    if (auto entry = m_impl->entries.find(static_cast<uint64>(texture_id));
        entry != m_impl->entries.end()) {
        entry->second.pending_removal = true;
    }
}

void ImGuiTextureRegistry::bind_render_texture(
    ImGuiTextureHandle handle,
    std::shared_ptr<const Texture> texture,
    std::shared_ptr<const Sampler> sampler
) {
    if (!handle) {
        fatal("ImGuiTextureRegistry received an invalid render texture handle");
    }
    upsert_texture(handle.raw_id(), std::move(texture), std::move(sampler));
}

void ImGuiTextureRegistry::unbind_render_texture(ImGuiTextureHandle handle) {
    if (handle) {
        unregister_texture(handle.texture_id());
    }
}

bool ImGuiTextureRegistry::contains(ImTextureID texture_id) const {
    return m_impl->entries.contains(static_cast<uint64>(texture_id));
}

bool ImGuiTextureRegistry::pending_removal(ImTextureID texture_id) const {
    const auto entry = m_impl->entries.find(static_cast<uint64>(texture_id));
    return entry != m_impl->entries.end() && entry->second.pending_removal;
}

std::size_t ImGuiTextureRegistry::size() const {
    return m_impl->entries.size();
}

void ImGuiTextureRegistry::register_managed_texture(
    uint64 texture_id,
    std::shared_ptr<const Texture> texture
) {
    upsert_texture(texture_id, std::move(texture));
}

void ImGuiTextureRegistry::upsert_texture(
    uint64 texture_id,
    std::shared_ptr<const Texture> texture,
    std::shared_ptr<const Sampler> sampler
) {
    if (!m_impl->device || !m_impl->texture_layout ||
        !m_impl->default_sampler) {
        fatal("ImGuiTextureRegistry is not initialized");
    }
    if (!texture || texture_id == static_cast<uint64>(ImTextureID_Invalid)) {
        fatal("ImGuiTextureRegistry received an invalid texture binding");
    }
    if (!sampler) {
        sampler = m_impl->default_sampler;
    }
    if (const auto existing = m_impl->entries.find(texture_id);
        existing != m_impl->entries.end() &&
        !existing->second.pending_removal &&
        existing->second.texture == texture &&
        existing->second.sampler == sampler) {
        return;
    }
    auto resource_set = m_impl->device->create_resource_set(
        ResourceSetDescription {
            .layout = m_impl->texture_layout,
            .resources = {texture, sampler},
            .name = "imgui_texture",
        }
    );
    m_impl->entries.insert_or_assign(
        texture_id,
        Impl::Entry {
            .texture = std::move(texture),
            .sampler = std::move(sampler),
            .resource_set = std::move(resource_set),
        }
    );
}

void ImGuiTextureRegistry::sync_images(
    const GraphicsDevice& device,
    const ExtractedImGuiImages& extracted_images,
    const RenderAssets<GpuImage>& gpu_images,
    const RenderingDefaults& defaults
) {
    if (m_impl->device != &device) {
        fatal("ImGuiTextureRegistry cannot sync images from another device");
    }

    std::unordered_set<uint64> desired_ids;
    desired_ids.reserve(extracted_images.bindings.size());
    for (const auto& binding : extracted_images.bindings) {
        desired_ids.insert(binding.texture_id);

        std::shared_ptr<const Texture> texture = defaults.default_texture;
        std::shared_ptr<const Sampler> sampler;
        if (const auto gpu_image = gpu_images.get(binding.image_id)) {
            texture = gpu_image->texture();
            sampler = gpu_image->sampler();
        }
        if (!texture) {
            error(
                "entisium-imgui has no fallback texture for image binding {}",
                binding.texture_id
            );
            continue;
        }
        upsert_texture(
            binding.texture_id,
            std::move(texture),
            std::move(sampler)
        );
    }

    for (const auto texture_id : m_impl->image_ids) {
        if (!desired_ids.contains(texture_id)) {
            unregister_texture(static_cast<ImTextureID>(texture_id));
        }
    }
    m_impl->image_ids = std::move(desired_ids);
}

std::shared_ptr<const ResourceSet>
ImGuiTextureRegistry::resource_set(uint64 texture_id) const {
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
    m_impl->image_ids.clear();
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
    std::unordered_map<uint64, std::shared_ptr<Texture>> managed_textures;
    std::unordered_set<uint64> missing_texture_errors;
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
                                            .offset = offsetof(
                                                ImGuiFrameVertex,
                                                position
                                            ),
                                            .format = VertexFormat::Float2,
                                        },
                                        VertexAttributeDescription {
                                            .location = 1,
                                            .offset =
                                                offsetof(ImGuiFrameVertex, uv),
                                            .format = VertexFormat::Float2,
                                        },
                                        VertexAttributeDescription {
                                            .location = 2,
                                            .offset = offsetof(
                                                ImGuiFrameVertex,
                                                color
                                            ),
                                            .format = VertexFormat::UByte4,
                                            .normalized = true,
                                        },
                                    },
                                .stride = sizeof(ImGuiFrameVertex),
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

void ImGuiRenderer::process_managed_textures(
    const GraphicsDevice& device,
    ImGuiTextureRegistry& registry,
    const std::vector<ImGuiTextureOperation>& operations
) {
    for (const auto& operation : operations) {
        if (operation.kind == ImGuiTextureOperationKind::Destroy) {
            registry.unregister_texture(
                static_cast<ImTextureID>(operation.texture_id)
            );
            m_impl->managed_textures.erase(operation.texture_id);
            continue;
        }

        if (operation.kind == ImGuiTextureOperationKind::Create) {
            auto texture = device.create_texture(
                TextureDescription {
                    .width = operation.width,
                    .height = operation.height,
                    .depth = 1,
                    .mip_level = 1,
                    .layer = 1,
                    .texture_format = PixelFormat::Rgba8Unorm,
                    .texture_usage = TextureUsage::Sampled,
                    .texture_type = TextureType::Texture2D,
                }
            );
            device.update_texture(
                texture,
                operation.pixels.data(),
                0,
                0,
                0,
                operation.width,
                operation.height,
                1,
                0,
                0
            );
            registry.register_managed_texture(operation.texture_id, texture);
            m_impl->managed_textures.emplace(
                operation.texture_id,
                std::move(texture)
            );
            continue;
        }

        if (operation.kind == ImGuiTextureOperationKind::Update) {
            const auto texture =
                m_impl->managed_textures.find(operation.texture_id);
            if (texture == m_impl->managed_textures.end()) {
                error(
                    "entisium-imgui cannot update unknown managed texture ID "
                    "{}",
                    operation.texture_id
                );
                continue;
            }
            device.update_texture(
                texture->second,
                operation.pixels.data(),
                operation.x,
                operation.y,
                0,
                operation.width,
                operation.height,
                1,
                0,
                0
            );
        }
    }
}

void ImGuiRenderer::render(
    const GraphicsDevice& device,
    PipelineCache& pipeline_cache,
    RenderFrameContext& frame_context,
    const MainSwapchain& main_swapchain,
    ImGuiTextureRegistry& texture_registry,
    const ImGuiFrameSnapshot& frame
) {
    process_managed_textures(
        device,
        texture_registry,
        frame.texture_operations
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
    const auto framebuffer_width =
        static_cast<int>(frame.display_size.x * frame.framebuffer_scale.x);
    const auto framebuffer_height =
        static_cast<int>(frame.display_size.y * frame.framebuffer_scale.y);
    if (!pipeline || framebuffer_width <= 0 || framebuffer_height <= 0 ||
        frame.vertices.empty() || frame.indices.empty()) {
        finish_overlay();
        return;
    }

    auto& slot = m_impl->slots[m_impl->next_slot];
    const auto vertex_size = frame.vertices.size() * sizeof(ImGuiFrameVertex);
    const auto index_size = frame.indices.size() * sizeof(uint32);
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
            2.0f / frame.display_size.x,
            -2.0f / frame.display_size.y,
        },
    };
    uniform.translate = Vector2 {
        -1.0f - frame.display_position.x * uniform.scale.x,
        1.0f - frame.display_position.y * uniform.scale.y,
    };

    auto* commands = frame_context.command_buffer();
    commands
        ->update_buffer(slot.vertex_buffer, frame.vertices.data(), vertex_size);
    commands
        ->update_buffer(slot.index_buffer, frame.indices.data(), index_size);
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

    for (const auto& draw_command : frame.commands) {
        if (draw_command.kind == ImGuiFrameCommandKind::ResetRenderState) {
            bind_render_state();
            continue;
        }
        const auto scissor = calculate_imgui_scissor(
            draw_command.clip_rect,
            frame.display_position,
            frame.framebuffer_scale,
            static_cast<uint32>(framebuffer_width),
            static_cast<uint32>(framebuffer_height)
        );
        if (!scissor) {
            continue;
        }

        auto texture_set =
            texture_registry.resource_set(draw_command.texture_id);
        if (!texture_set) {
            if (m_impl->missing_texture_errors.insert(draw_command.texture_id)
                    .second) {
                error(
                    "entisium-imgui draw references unregistered texture ID {}",
                    draw_command.texture_id
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
        commands->draw_indexed(
            draw_command.element_count,
            draw_command.first_index,
            draw_command.vertex_offset
        );
    }
    commands->end_render_pass();
    m_impl->next_slot = (m_impl->next_slot + 1) % m_impl->slots.size();
    finish_overlay();
}

void ImGuiRenderer::shutdown(ImGuiTextureRegistry& texture_registry) noexcept {
    m_impl->managed_textures.clear();
    m_impl->missing_texture_errors.clear();
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

} // namespace ets
