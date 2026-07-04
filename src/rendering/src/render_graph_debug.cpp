#include "rendering/render_graph.hpp"

#include <format>
#include <string_view>

namespace fei {

namespace {

std::string handle_name(RgTextureHandle handle) {
    return std::format("{}:{}", handle.index, handle.generation);
}

std::string_view access_name(RenderGraphAccess access) {
    switch (access) {
        case RenderGraphAccess::TextureRead:
            return "texture_read";
        case RenderGraphAccess::ColorAttachmentWrite:
            return "color_attachment_write";
        case RenderGraphAccess::DepthStencilWrite:
            return "depth_stencil_write";
        case RenderGraphAccess::TextureReadWrite:
            return "texture_read_write";
        case RenderGraphAccess::BlitSource:
            return "blit_source";
    }
    return "unknown";
}

std::string_view pixel_format_name(PixelFormat format) {
    switch (format) {
#define FEI_PIXEL_FORMAT_NAME(value) \
    case PixelFormat::value:         \
        return #value
        FEI_PIXEL_FORMAT_NAME(R8Unorm);
        FEI_PIXEL_FORMAT_NAME(R8Snorm);
        FEI_PIXEL_FORMAT_NAME(R8Uint);
        FEI_PIXEL_FORMAT_NAME(R8Sint);
        FEI_PIXEL_FORMAT_NAME(R16Uint);
        FEI_PIXEL_FORMAT_NAME(R16Sint);
        FEI_PIXEL_FORMAT_NAME(R16Unorm);
        FEI_PIXEL_FORMAT_NAME(R16Snorm);
        FEI_PIXEL_FORMAT_NAME(R16Float);
        FEI_PIXEL_FORMAT_NAME(Rg8Unorm);
        FEI_PIXEL_FORMAT_NAME(Rg8Snorm);
        FEI_PIXEL_FORMAT_NAME(Rg8Uint);
        FEI_PIXEL_FORMAT_NAME(Rg8Sint);
        FEI_PIXEL_FORMAT_NAME(R32Uint);
        FEI_PIXEL_FORMAT_NAME(R32Sint);
        FEI_PIXEL_FORMAT_NAME(R32Float);
        FEI_PIXEL_FORMAT_NAME(Rg16Uint);
        FEI_PIXEL_FORMAT_NAME(Rg16Sint);
        FEI_PIXEL_FORMAT_NAME(Rg16Unorm);
        FEI_PIXEL_FORMAT_NAME(Rg16Snorm);
        FEI_PIXEL_FORMAT_NAME(Rg16Float);
        FEI_PIXEL_FORMAT_NAME(Rgba8Unorm);
        FEI_PIXEL_FORMAT_NAME(Rgba8UnormSrgb);
        FEI_PIXEL_FORMAT_NAME(Rgba8Snorm);
        FEI_PIXEL_FORMAT_NAME(Rgba8Uint);
        FEI_PIXEL_FORMAT_NAME(Rgba8Sint);
        FEI_PIXEL_FORMAT_NAME(Bgra8Unorm);
        FEI_PIXEL_FORMAT_NAME(Bgra8UnormSrgb);
        FEI_PIXEL_FORMAT_NAME(Rgb9e5Ufloat);
        FEI_PIXEL_FORMAT_NAME(Rgb10a2Uint);
        FEI_PIXEL_FORMAT_NAME(Rgb10a2Unorm);
        FEI_PIXEL_FORMAT_NAME(Rg11b10Ufloat);
        FEI_PIXEL_FORMAT_NAME(Rg32Uint);
        FEI_PIXEL_FORMAT_NAME(Rg32Sint);
        FEI_PIXEL_FORMAT_NAME(Rg32Float);
        FEI_PIXEL_FORMAT_NAME(Rgba16Uint);
        FEI_PIXEL_FORMAT_NAME(Rgba16Sint);
        FEI_PIXEL_FORMAT_NAME(Rgba16Unorm);
        FEI_PIXEL_FORMAT_NAME(Rgba16Snorm);
        FEI_PIXEL_FORMAT_NAME(Rgba16Float);
        FEI_PIXEL_FORMAT_NAME(Rgba32Uint);
        FEI_PIXEL_FORMAT_NAME(Rgba32Sint);
        FEI_PIXEL_FORMAT_NAME(Rgba32Float);
        FEI_PIXEL_FORMAT_NAME(Stencil8);
        FEI_PIXEL_FORMAT_NAME(Depth16Unorm);
        FEI_PIXEL_FORMAT_NAME(Depth24Plus);
        FEI_PIXEL_FORMAT_NAME(Depth24PlusStencil8);
        FEI_PIXEL_FORMAT_NAME(Depth32Float);
        FEI_PIXEL_FORMAT_NAME(Depth32FloatStencil8);
        FEI_PIXEL_FORMAT_NAME(Bc1RgbaUnorm);
        FEI_PIXEL_FORMAT_NAME(Bc1RgbaUnormSrgb);
        FEI_PIXEL_FORMAT_NAME(Bc2RgbaUnorm);
        FEI_PIXEL_FORMAT_NAME(Bc2RgbaUnormSrgb);
        FEI_PIXEL_FORMAT_NAME(Bc3RgbaUnorm);
        FEI_PIXEL_FORMAT_NAME(Bc3RgbaUnormSrgb);
        FEI_PIXEL_FORMAT_NAME(Bc4RUnorm);
        FEI_PIXEL_FORMAT_NAME(Bc4RSnorm);
        FEI_PIXEL_FORMAT_NAME(Bc5RgUnorm);
        FEI_PIXEL_FORMAT_NAME(Bc5RgSnorm);
        FEI_PIXEL_FORMAT_NAME(Bc6hRgbUfloat);
        FEI_PIXEL_FORMAT_NAME(Bc6hRgbFloat);
        FEI_PIXEL_FORMAT_NAME(Bc7RgbaUnorm);
        FEI_PIXEL_FORMAT_NAME(Bc7RgbaUnormSrgb);
        FEI_PIXEL_FORMAT_NAME(Etc2Rgb8Unorm);
        FEI_PIXEL_FORMAT_NAME(Etc2Rgb8UnormSrgb);
        FEI_PIXEL_FORMAT_NAME(Etc2Rgb8A1Unorm);
        FEI_PIXEL_FORMAT_NAME(Etc2Rgb8A1UnormSrgb);
        FEI_PIXEL_FORMAT_NAME(Etc2Rgba8Unorm);
        FEI_PIXEL_FORMAT_NAME(Etc2Rgba8UnormSrgb);
        FEI_PIXEL_FORMAT_NAME(EacR11Unorm);
        FEI_PIXEL_FORMAT_NAME(EacR11Snorm);
        FEI_PIXEL_FORMAT_NAME(EacRg11Unorm);
        FEI_PIXEL_FORMAT_NAME(EacRg11Snorm);
#undef FEI_PIXEL_FORMAT_NAME
    }
    return "Unknown";
}

std::string_view texture_type_name(TextureType type) {
    switch (type) {
        case TextureType::Texture1D:
            return "Texture1D";
        case TextureType::Texture2D:
            return "Texture2D";
        case TextureType::Texture3D:
            return "Texture3D";
    }
    return "Unknown";
}

std::string texture_usage_name(BitFlags<TextureUsage> usage) {
    std::string result;
    auto append = [&](TextureUsage flag, std::string_view name) {
        if (!usage.is_set(flag)) {
            return;
        }
        if (!result.empty()) {
            result += "|";
        }
        result += name;
    };
    append(TextureUsage::Sampled, "Sampled");
    append(TextureUsage::Storage, "Storage");
    append(TextureUsage::RenderTarget, "RenderTarget");
    append(TextureUsage::DepthStencil, "DepthStencil");
    append(TextureUsage::Cubemap, "Cubemap");
    append(TextureUsage::Staging, "Staging");
    append(TextureUsage::GenerateMipmaps, "GenerateMipmaps");
    return result.empty() ? "None" : result;
}

} // namespace

void RenderGraph::update_debug_info(
    const std::vector<std::vector<uint32>>& incoming
) {
    RgDebugInfo debug;
    debug.compiled = m_compiled;
    debug.compile_error = m_compile_error;
    debug.active_order = m_compiled_order;
    debug.stats = m_stats;

    debug.active_pass_names.reserve(m_compiled_order.size());
    for (auto pass_index : m_compiled_order) {
        if (pass_index < m_passes.size()) {
            debug.active_pass_names.push_back(m_passes[pass_index]->name);
        }
    }

    auto texture_use_debug_info = [&](const TextureUse& texture_use) {
        RgTextureUseDebugInfo info {
            .handle = texture_use.handle,
            .access = texture_use.access,
            .access_name = std::string(access_name(texture_use.access)),
        };
        if (is_valid(texture_use.handle)) {
            info.texture_name = texture_resource(texture_use.handle).name;
        } else {
            info.texture_name =
                std::format("<invalid:{}>", handle_name(texture_use.handle));
        }
        return info;
    };

    debug.passes.reserve(m_passes.size());
    for (uint32 pass_index = 0; pass_index < m_passes.size(); ++pass_index) {
        const auto& pass = *m_passes[pass_index];
        RgPassDebugInfo pass_info {
            .index = pass_index,
            .name = pass.name,
            .active = pass.active,
            .side_effect = pass.side_effect,
        };
        if (pass_index < incoming.size()) {
            pass_info.dependencies = incoming[pass_index];
        }
        pass_info.reads.reserve(pass.reads.size());
        for (const auto& read : pass.reads) {
            pass_info.reads.push_back(texture_use_debug_info(read));
        }
        pass_info.writes.reserve(pass.writes.size());
        for (const auto& write : pass.writes) {
            pass_info.writes.push_back(texture_use_debug_info(write));
        }
        debug.passes.push_back(std::move(pass_info));
    }

    debug.textures.reserve(m_textures.size());
    for (uint32 texture_index = 0; texture_index < m_textures.size();
         ++texture_index) {
        const auto& texture = m_textures[texture_index];
        const auto active =
            texture.first_active_use != RgTextureHandle::InvalidIndex;
        debug.textures.push_back(
            RgTextureDebugInfo {
                .index = texture_index,
                .name = texture.name,
                .active = active,
                .imported = texture.imported,
                .width = texture.description.width,
                .height = texture.description.height,
                .depth = texture.description.depth,
                .mip_level = texture.description.mip_level,
                .layer = texture.description.layer,
                .format = std::string(
                    pixel_format_name(texture.description.texture_format)
                ),
                .usage = texture_usage_name(texture.description.texture_usage),
                .type = std::string(
                    texture_type_name(texture.description.texture_type)
                ),
                .version_count = static_cast<uint32>(texture.versions.size()),
                .first_active_use = texture.first_active_use,
                .last_active_use = texture.last_active_use,
            }
        );
    }

    debug.resource_sets.reserve(m_resource_sets.size());
    for (uint32 resource_set_index = 0;
         resource_set_index < m_resource_sets.size();
         ++resource_set_index) {
        const auto& resource_set = m_resource_sets[resource_set_index];
        const auto active = resource_set.pass_index < m_passes.size() &&
                            m_passes[resource_set.pass_index]->active;
        RgResourceSetDebugInfo resource_set_info {
            .index = resource_set_index,
            .generation = resource_set.generation,
            .pass_index = resource_set.pass_index,
            .name = resource_set.name,
            .active = active,
            .resolved = resource_set.resource_set != nullptr,
            .has_layout = resource_set.layout != nullptr,
        };
        resource_set_info.bindings.reserve(resource_set.bindings.size());
        for (uint32 binding_index = 0;
             binding_index < resource_set.bindings.size();
             ++binding_index) {
            const auto& binding = resource_set.bindings[binding_index];
            if (binding.is_texture()) {
                const auto texture = binding.texture();
                resource_set_info.bindings.push_back(
                    RgResourceSetBindingDebugInfo {
                        .index = binding_index,
                        .kind = "texture",
                        .resource_name = is_valid(texture) ?
                                             texture_resource(texture).name :
                                             std::format(
                                                 "<invalid:{}>",
                                                 handle_name(texture)
                                             ),
                        .valid = is_valid(texture),
                        .texture = texture,
                    }
                );
            } else {
                const auto& resource = binding.resource();
                resource_set_info.bindings.push_back(
                    RgResourceSetBindingDebugInfo {
                        .index = binding_index,
                        .kind = "external",
                        .resource_name = resource ? "external" : "null",
                        .valid = resource != nullptr,
                    }
                );
            }
        }
        debug.resource_sets.push_back(std::move(resource_set_info));
    }

    m_debug_info = std::move(debug);
}

void RenderGraph::sync_debug_stats() {
    m_debug_info.stats = m_stats;
    for (std::size_t i = 0;
         i < m_debug_info.resource_sets.size() && i < m_resource_sets.size();
         ++i) {
        m_debug_info.resource_sets[i].resolved =
            m_resource_sets[i].resource_set != nullptr;
    }
}

} // namespace fei
