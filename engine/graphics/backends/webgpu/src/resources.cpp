#include "graphics_webgpu/resources.hpp"

#include "base/log.hpp"
#include "graphics_webgpu/utils.hpp"
#include "mipmap_generator.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace fei {

namespace {

WGPUBufferUsage buffer_usage(const BufferDescription& desc) {
    WGPUBufferUsage result = WGPUBufferUsage_CopyDst;
    if (desc.usages.is_set(BufferUsages::Vertex)) {
        result |= WGPUBufferUsage_Vertex;
    }
    if (desc.usages.is_set(BufferUsages::Index)) {
        result |= WGPUBufferUsage_Index;
    }
    if (desc.usages.is_set(BufferUsages::Uniform)) {
        result |= WGPUBufferUsage_Uniform;
    }
    if (desc.usages.is_set(BufferUsages::Storage)) {
        result |= WGPUBufferUsage_Storage;
    }
    if (desc.usages.is_set(BufferUsages::Indirect)) {
        result |= WGPUBufferUsage_Indirect;
    }
    if (desc.usages.is_set(BufferUsages::Staging)) {
        result |= WGPUBufferUsage_MapRead;
    }
    return result;
}

WGPUTextureUsage texture_usage(const TextureDescription& desc) {
    WGPUTextureUsage result =
        WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
    if (desc.texture_usage.is_set(TextureUsage::Sampled)) {
        result |= WGPUTextureUsage_TextureBinding;
    }
    if (desc.texture_usage.is_set(TextureUsage::Storage)) {
        result |= WGPUTextureUsage_StorageBinding;
    }
    if (desc.texture_usage.is_set(TextureUsage::GenerateMipmaps)) {
        result |=
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding;
    }
    if (desc.texture_usage.is_set(TextureUsage::RenderTarget) ||
        desc.texture_usage.is_set(TextureUsage::DepthStencil)) {
        result |= WGPUTextureUsage_RenderAttachment;
    }
    return result;
}

WGPUTextureDimension texture_dimension(TextureType value) {
    switch (value) {
        case TextureType::Texture1D:
            return WGPUTextureDimension_1D;
        case TextureType::Texture2D:
            return WGPUTextureDimension_2D;
        case TextureType::Texture3D:
            return WGPUTextureDimension_3D;
    }
    fatal("Unsupported WebGPU texture dimension");
}

WGPUTextureViewDimension default_view_dimension(const Texture& texture) {
    if (texture.usage().is_set(TextureUsage::Cubemap)) {
        return texture.layer() > 1 ? WGPUTextureViewDimension_CubeArray :
                                     WGPUTextureViewDimension_Cube;
    }
    switch (texture.type()) {
        case TextureType::Texture1D:
            if (texture.layer() > 1) {
                fatal("WebGPU does not support 1D texture arrays");
            }
            return WGPUTextureViewDimension_1D;
        case TextureType::Texture2D:
            return texture.layer() > 1 ? WGPUTextureViewDimension_2DArray :
                                         WGPUTextureViewDimension_2D;
        case TextureType::Texture3D:
            return WGPUTextureViewDimension_3D;
    }
    fatal("Unsupported WebGPU texture view dimension");
}

WGPUTextureViewDimension texture_view_dimension(TextureViewDimension value) {
    switch (value) {
        case TextureViewDimension::Texture1D:
            return WGPUTextureViewDimension_1D;
        case TextureViewDimension::Texture2D:
            return WGPUTextureViewDimension_2D;
        case TextureViewDimension::Texture2DArray:
            return WGPUTextureViewDimension_2DArray;
        case TextureViewDimension::Cube:
            return WGPUTextureViewDimension_Cube;
        case TextureViewDimension::CubeArray:
            return WGPUTextureViewDimension_CubeArray;
        case TextureViewDimension::Texture3D:
            return WGPUTextureViewDimension_3D;
    }
    fatal("Unsupported WebGPU texture view dimension");
}

WGPUAddressMode address_mode(SamplerAddressMode value) {
    switch (value) {
        case SamplerAddressMode::Repeat:
            return WGPUAddressMode_Repeat;
        case SamplerAddressMode::MirrorRepeat:
            return WGPUAddressMode_MirrorRepeat;
        case SamplerAddressMode::ClampToEdge:
        case SamplerAddressMode::ClampToBorder:
            return WGPUAddressMode_ClampToEdge;
    }
    fatal("Unsupported WebGPU sampler address mode");
}

WGPUFilterMode filter_mode(SamplerFilter value) {
    return value == SamplerFilter::Nearest ? WGPUFilterMode_Nearest :
                                             WGPUFilterMode_Linear;
}

WGPUMipmapFilterMode mip_filter_mode(SamplerFilter value) {
    return value == SamplerFilter::Nearest ? WGPUMipmapFilterMode_Nearest :
                                             WGPUMipmapFilterMode_Linear;
}

WGPUBlendFactor blend_factor(BlendFactor value) {
    switch (value) {
        case BlendFactor::Zero:
            return WGPUBlendFactor_Zero;
        case BlendFactor::One:
            return WGPUBlendFactor_One;
        case BlendFactor::SrcColor:
            return WGPUBlendFactor_Src;
        case BlendFactor::OneMinusSrcColor:
            return WGPUBlendFactor_OneMinusSrc;
        case BlendFactor::SrcAlpha:
            return WGPUBlendFactor_SrcAlpha;
        case BlendFactor::OneMinusSrcAlpha:
            return WGPUBlendFactor_OneMinusSrcAlpha;
        case BlendFactor::DstColor:
            return WGPUBlendFactor_Dst;
        case BlendFactor::OneMinusDstColor:
            return WGPUBlendFactor_OneMinusDst;
        case BlendFactor::DstAlpha:
            return WGPUBlendFactor_DstAlpha;
        case BlendFactor::OneMinusDstAlpha:
            return WGPUBlendFactor_OneMinusDstAlpha;
    }
    fatal("Unsupported WebGPU blend factor");
}

WGPUBlendOperation blend_operation(BlendFunction value) {
    switch (value) {
        case BlendFunction::Add:
            return WGPUBlendOperation_Add;
        case BlendFunction::Subtract:
            return WGPUBlendOperation_Subtract;
        case BlendFunction::ReverseSubtract:
            return WGPUBlendOperation_ReverseSubtract;
        case BlendFunction::Min:
            return WGPUBlendOperation_Min;
        case BlendFunction::Max:
            return WGPUBlendOperation_Max;
    }
    fatal("Unsupported WebGPU blend operation");
}

WGPUColorWriteMask color_write_mask(ColorWriteMask value) {
    const auto raw = static_cast<std::uint8_t>(value);
    WGPUColorWriteMask result = WGPUColorWriteMask_None;
    if ((raw & static_cast<std::uint8_t>(ColorWriteMask::Red)) != 0) {
        result |= WGPUColorWriteMask_Red;
    }
    if ((raw & static_cast<std::uint8_t>(ColorWriteMask::Green)) != 0) {
        result |= WGPUColorWriteMask_Green;
    }
    if ((raw & static_cast<std::uint8_t>(ColorWriteMask::Blue)) != 0) {
        result |= WGPUColorWriteMask_Blue;
    }
    if ((raw & static_cast<std::uint8_t>(ColorWriteMask::Alpha)) != 0) {
        result |= WGPUColorWriteMask_Alpha;
    }
    return result;
}

WGPUPrimitiveTopology primitive_topology(RenderPrimitive value) {
    switch (value) {
        case RenderPrimitive::Point:
            return WGPUPrimitiveTopology_PointList;
        case RenderPrimitive::Lines:
            return WGPUPrimitiveTopology_LineList;
        case RenderPrimitive::LineStrip:
            return WGPUPrimitiveTopology_LineStrip;
        case RenderPrimitive::Triangles:
            return WGPUPrimitiveTopology_TriangleList;
        case RenderPrimitive::TrianglesStrip:
            return WGPUPrimitiveTopology_TriangleStrip;
    }
    fatal("Unsupported WebGPU primitive topology");
}

WGPUCullMode cull_mode(CullMode value) {
    switch (value) {
        case CullMode::None:
            return WGPUCullMode_None;
        case CullMode::Back:
            return WGPUCullMode_Back;
        case CullMode::Front:
            return WGPUCullMode_Front;
    }
    fatal("Unsupported WebGPU cull mode");
}

std::shared_ptr<const ShaderModuleWebGpu>
find_shader(const ShaderProgramDescription& program, ShaderStages stage) {
    for (const auto& shader : program.shaders) {
        if (shader->stage() == stage) {
            auto result =
                std::dynamic_pointer_cast<const ShaderModuleWebGpu>(shader);
            if (!result) {
                fatal("WebGPU pipeline received a shader from another backend");
            }
            return result;
        }
    }
    return nullptr;
}

std::vector<WGPUBindGroupLayout> bind_group_layouts(
    const std::vector<std::shared_ptr<const ResourceLayout>>& layouts
) {
    std::vector<WGPUBindGroupLayout> result;
    result.reserve(layouts.size());
    for (const auto& layout : layouts) {
        auto webgpu_layout =
            std::dynamic_pointer_cast<const ResourceLayoutWebGpu>(layout);
        if (!webgpu_layout) {
            fatal("WebGPU pipeline received a layout from another backend");
        }
        result.push_back(webgpu_layout->handle());
    }
    return result;
}

WGPUPipelineLayout create_pipeline_layout(
    WGPUDevice device,
    const std::vector<std::shared_ptr<const ResourceLayout>>& layouts
) {
    auto handles = bind_group_layouts(layouts);
    WGPUPipelineLayoutDescriptor descriptor {};
    descriptor.label = {"fei pipeline layout", WGPU_STRLEN};
    descriptor.bindGroupLayoutCount = handles.size();
    descriptor.bindGroupLayouts = handles.data();
    auto result = wgpuDeviceCreatePipelineLayout(device, &descriptor);
    if (result == nullptr) {
        fatal("Failed to create WebGPU pipeline layout");
    }
    return result;
}

} // namespace

BufferWebGpu::BufferWebGpu(
    std::shared_ptr<WebGpuDeviceState> state,
    const BufferDescription& desc
) : m_state(std::move(state)), m_desc(desc) {
    WGPUBufferDescriptor descriptor {};
    descriptor.label = {"fei buffer", WGPU_STRLEN};
    descriptor.usage = buffer_usage(desc);
    descriptor.size = desc.size;
    m_buffer = wgpuDeviceCreateBuffer(m_state->device(), &descriptor);
    if (m_buffer == nullptr) {
        fatal("Failed to create WebGPU buffer");
    }
}

BufferWebGpu::~BufferWebGpu() {
    if (m_buffer != nullptr) {
        wgpuBufferDestroy(m_buffer);
        wgpuBufferRelease(m_buffer);
    }
}

TextureWebGpu::TextureWebGpu(
    std::shared_ptr<WebGpuDeviceState> state,
    const TextureDescription& desc
) : m_state(std::move(state)), m_desc(desc) {
    if (desc.texture_usage.is_set(TextureUsage::GenerateMipmaps) &&
        !supports_webgpu_mipmap_generation(desc)) {
        fatal(
            "WebGPU mipmap generation currently requires a single-sampled "
            "Rgba32Float 2D texture"
        );
    }
    const auto array_layers = desc.texture_usage.is_set(TextureUsage::Cubemap) ?
                                  desc.layer * 6 :
                                  desc.layer;
    WGPUTextureDescriptor descriptor {};
    descriptor.label = {"fei texture", WGPU_STRLEN};
    descriptor.usage = texture_usage(desc);
    descriptor.dimension = texture_dimension(desc.texture_type);
    descriptor.size = {
        desc.width,
        desc.height,
        desc.texture_type == TextureType::Texture3D ? desc.depth : array_layers,
    };
    descriptor.format = to_webgpu(desc.texture_format);
    descriptor.mipLevelCount = desc.mip_level;
    descriptor.sampleCount = static_cast<uint32>(desc.sample_count);
    m_texture = wgpuDeviceCreateTexture(m_state->device(), &descriptor);
    if (m_texture == nullptr) {
        fatal("Failed to create WebGPU texture");
    }
}

TextureWebGpu::TextureWebGpu(
    std::shared_ptr<WebGpuDeviceState> state,
    const TextureDescription& desc,
    WGPUTexture texture
) :
    m_state(std::move(state)), m_desc(desc), m_texture(texture),
    m_release_on_destroy(false) {
    if (m_texture == nullptr) {
        fatal("Cannot wrap a null WebGPU texture");
    }
}

TextureWebGpu::~TextureWebGpu() {
    if (m_release_on_destroy && m_texture != nullptr) {
        wgpuTextureRelease(m_texture);
    }
}

TextureViewWebGpu::TextureViewWebGpu(
    std::shared_ptr<WebGpuDeviceState> state,
    const TextureViewDescription& desc
) : TextureView(desc), m_state(std::move(state)) {
    const auto texture =
        std::dynamic_pointer_cast<const TextureWebGpu>(desc.target);
    if (!texture) {
        fatal("WebGPU texture view received a texture from another backend");
    }
    auto dimension = desc.view_type ? to_webgpu(desc.view_type.value()) :
                                      default_view_dimension(*desc.target);
    auto array_layers = desc.array_layers;
    auto base_array_layer = desc.base_array_layer;
    if (desc.target->usage().is_set(TextureUsage::Cubemap)) {
        array_layers *= 6;
        base_array_layer *= 6;
    }
    WGPUTextureViewDescriptor descriptor {};
    descriptor.label = {"fei texture view", WGPU_STRLEN};
    descriptor.format = to_webgpu(format());
    descriptor.dimension = dimension;
    descriptor.baseMipLevel = desc.base_mip_level;
    descriptor.mipLevelCount = desc.mip_levels;
    descriptor.baseArrayLayer = base_array_layer;
    descriptor.arrayLayerCount = array_layers;
    descriptor.aspect = WGPUTextureAspect_All;
    descriptor.usage = WGPUTextureUsage_None;
    m_view = wgpuTextureCreateView(texture->handle(), &descriptor);
    if (m_view == nullptr) {
        fatal("Failed to create WebGPU texture view");
    }
}

TextureViewWebGpu::~TextureViewWebGpu() {
    if (m_view != nullptr) {
        wgpuTextureViewRelease(m_view);
    }
}

ShaderModuleWebGpu::ShaderModuleWebGpu(
    std::shared_ptr<WebGpuDeviceState> state,
    const ShaderDescription& desc
) : ShaderModule(desc), m_state(std::move(state)) {
    if (!desc.wgsl.empty()) {
        switch (desc.stage) {
            case ShaderStages::Vertex:
                m_entry_point = "vertex_main";
                break;
            case ShaderStages::Fragment:
                m_entry_point = "fragment_main";
                break;
            case ShaderStages::Compute:
                m_entry_point = "compute_main";
                break;
            default:
                fatal("WebGPU shader {} has an unsupported stage", desc.path);
        }
    }
    WGPUShaderModuleDescriptor descriptor {};
    descriptor.label = {desc.path.c_str(), desc.path.size()};
    WGPUShaderSourceWGSL wgsl_source {
        .chain = {.sType = WGPUSType_ShaderSourceWGSL},
    };
    WGPUShaderSourceSPIRV spirv_source {
        .chain = {.sType = WGPUSType_ShaderSourceSPIRV},
    };
    if (!desc.wgsl.empty()) {
        wgsl_source.code = {desc.wgsl.c_str(), desc.wgsl.size()};
        descriptor.nextInChain = &wgsl_source.chain;
    } else {
        if (desc.spirv.empty() ||
            desc.spirv.size() % sizeof(std::uint32_t) != 0) {
            fatal("WebGPU shader {} has no WGSL or valid SPIR-V", desc.path);
        }
        spirv_source.codeSize = static_cast<std::uint32_t>(
            desc.spirv.size() / sizeof(std::uint32_t)
        );
        spirv_source.code =
            reinterpret_cast<const std::uint32_t*>(desc.spirv.data());
        descriptor.nextInChain = &spirv_source.chain;
    }
    wgpuDevicePushErrorScope(m_state->device(), WGPUErrorFilter_Validation);
    m_module = wgpuDeviceCreateShaderModule(m_state->device(), &descriptor);
    check_webgpu_error_scope(
        *m_state,
        "WebGPU shader module creation for " + desc.path
    );
    if (m_module == nullptr) {
        fatal("Failed to create WebGPU shader module {}", desc.path);
    }
}

ShaderModuleWebGpu::~ShaderModuleWebGpu() {
    if (m_module != nullptr) {
        wgpuShaderModuleRelease(m_module);
    }
}

SamplerWebGpu::SamplerWebGpu(
    std::shared_ptr<WebGpuDeviceState> state,
    const SamplerDescription& desc
) : m_state(std::move(state)) {
    WGPUSamplerDescriptor descriptor {};
    descriptor.label = {"fei sampler", WGPU_STRLEN};
    descriptor.addressModeU = address_mode(desc.address_mode_u);
    descriptor.addressModeV = address_mode(desc.address_mode_v);
    descriptor.addressModeW = address_mode(desc.address_mode_w);
    descriptor.magFilter = filter_mode(desc.mag_filter);
    descriptor.minFilter = filter_mode(desc.min_filter);
    descriptor.mipmapFilter = mip_filter_mode(desc.mipmap_filter);
    descriptor.lodMinClamp = desc.min_lod;
    descriptor.lodMaxClamp = desc.max_lod;
    descriptor.compare = desc.comparison_kind ?
                             to_webgpu(desc.comparison_kind.value()) :
                             WGPUCompareFunction_Undefined;
    descriptor.maxAnisotropy =
        static_cast<std::uint16_t>(std::max(1.0f, desc.max_anisotropy));
    m_sampler = wgpuDeviceCreateSampler(m_state->device(), &descriptor);
    if (m_sampler == nullptr) {
        fatal("Failed to create WebGPU sampler");
    }
}

SamplerWebGpu::~SamplerWebGpu() {
    if (m_sampler != nullptr) {
        wgpuSamplerRelease(m_sampler);
    }
}

ResourceLayoutWebGpu::ResourceLayoutWebGpu(
    std::shared_ptr<WebGpuDeviceState> state,
    const ResourceLayoutDescription& desc
) : ResourceLayout(desc), m_state(std::move(state)), m_desc(desc) {
    std::vector<WGPUBindGroupLayoutEntry> entries;
    entries.reserve(desc.elements.size());
    for (const auto& element : desc.elements) {
        if (element.array_count != 1) {
            fatal(
                "WebGPU resource arrays are not yet supported for '{}'",
                element.name
            );
        }
        WGPUBindGroupLayoutEntry entry {};
        entry.binding = element.binding;
        entry.visibility = to_webgpu_shader_stages(element.stages);
        switch (element.kind) {
            case ResourceKind::UniformBuffer:
                entry.buffer.type = WGPUBufferBindingType_Uniform;
                entry.buffer.hasDynamicOffset = element.options.is_set(
                    ResourceLayoutElementOptions::DynamicBinding
                );
                break;
            case ResourceKind::StorageBufferReadOnly:
                entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
                entry.buffer.hasDynamicOffset = element.options.is_set(
                    ResourceLayoutElementOptions::DynamicBinding
                );
                break;
            case ResourceKind::StorageBufferReadWrite:
                entry.buffer.type = WGPUBufferBindingType_Storage;
                entry.buffer.hasDynamicOffset = element.options.is_set(
                    ResourceLayoutElementOptions::DynamicBinding
                );
                break;
            case ResourceKind::TextureReadOnly:
                entry.texture.sampleType = WGPUTextureSampleType_Float;
                entry.texture.viewDimension =
                    texture_view_dimension(element.texture_dimension);
                break;
            case ResourceKind::TextureReadWrite:
                entry.storageTexture.access =
                    WGPUStorageTextureAccess_ReadWrite;
                entry.storageTexture.format = WGPUTextureFormat_RGBA8Unorm;
                entry.storageTexture.viewDimension =
                    texture_view_dimension(element.texture_dimension);
                break;
            case ResourceKind::Sampler:
                entry.sampler.type = WGPUSamplerBindingType_Filtering;
                break;
        }
        entries.push_back(entry);
    }
    WGPUBindGroupLayoutDescriptor descriptor {};
    descriptor.label = {"fei bind group layout", WGPU_STRLEN};
    descriptor.entryCount = entries.size();
    descriptor.entries = entries.data();
    m_layout = wgpuDeviceCreateBindGroupLayout(m_state->device(), &descriptor);
    if (m_layout == nullptr) {
        fatal("Failed to create WebGPU bind group layout");
    }
}

ResourceLayoutWebGpu::~ResourceLayoutWebGpu() {
    if (m_layout != nullptr) {
        wgpuBindGroupLayoutRelease(m_layout);
    }
}

void ResourceLayoutWebGpu::adopt_pipeline_layout(
    WGPUBindGroupLayout layout
) const {
    if (layout == nullptr) {
        fatal("Cannot adopt a null WebGPU bind group layout");
    }
    if (m_layout != nullptr) {
        wgpuBindGroupLayoutRelease(m_layout);
    }
    m_layout = layout;
}

ResourceSetWebGpu::ResourceSetWebGpu(
    std::shared_ptr<WebGpuDeviceState> state,
    const ResourceSetDescription& desc
) : ResourceSet(desc), m_state(std::move(state)) {
    auto layout =
        std::dynamic_pointer_cast<const ResourceLayoutWebGpu>(desc.layout);
    if (!layout) {
        fatal("WebGPU resource set received a layout from another backend");
    }
    if (desc.resources.size() != layout->description().elements.size()) {
        fatal(
            "WebGPU resource set '{}' expected {} resources, got {}",
            desc.name,
            layout->description().elements.size(),
            desc.resources.size()
        );
    }

    std::vector<WGPUBindGroupEntry> entries;
    entries.reserve(desc.resources.size());
    for (std::size_t index = 0; index < desc.resources.size(); ++index) {
        const auto& resource = desc.resources[index];
        const auto& element = layout->description().elements[index];
        WGPUBindGroupEntry entry {};
        entry.binding = element.binding;
        switch (element.kind) {
            case ResourceKind::UniformBuffer:
            case ResourceKind::StorageBufferReadOnly:
            case ResourceKind::StorageBufferReadWrite: {
                std::shared_ptr<const Buffer> buffer;
                std::size_t offset = 0;
                std::size_t size = BufferRange::WholeSize;
                if (auto range = std::dynamic_pointer_cast<const BufferRange>(
                        resource
                    )) {
                    buffer = range->buffer();
                    offset = range->offset();
                    size = range->size();
                } else {
                    buffer = std::dynamic_pointer_cast<const Buffer>(resource);
                }
                auto webgpu_buffer =
                    std::dynamic_pointer_cast<const BufferWebGpu>(buffer);
                if (!webgpu_buffer) {
                    fatal(
                        "WebGPU resource '{}' is not a WebGPU buffer",
                        element.name
                    );
                }
                entry.buffer = webgpu_buffer->handle();
                entry.offset = offset;
                entry.size = size == BufferRange::WholeSize ?
                                 webgpu_buffer->size() - offset :
                                 size;
                break;
            }
            case ResourceKind::TextureReadOnly:
            case ResourceKind::TextureReadWrite: {
                auto view = std::dynamic_pointer_cast<const TextureViewWebGpu>(
                    resource
                );
                if (view) {
                    entry.textureView = view->handle();
                    break;
                }
                auto texture =
                    std::dynamic_pointer_cast<const TextureWebGpu>(resource);
                if (!texture) {
                    fatal(
                        "WebGPU resource '{}' is not a WebGPU texture",
                        element.name
                    );
                }
                TextureViewDescription view_desc {
                    .target = texture,
                    .base_mip_level = 0,
                    .mip_levels = texture->mip_level(),
                    .base_array_layer = 0,
                    .array_layers = texture->layer(),
                    .format = texture->format(),
                };
                auto owned_view =
                    std::make_shared<TextureViewWebGpu>(m_state, view_desc);
                entry.textureView = owned_view->handle();
                m_owned_views.push_back(std::move(owned_view));
                break;
            }
            case ResourceKind::Sampler: {
                auto sampler =
                    std::dynamic_pointer_cast<const SamplerWebGpu>(resource);
                if (!sampler) {
                    fatal(
                        "WebGPU resource '{}' is not a WebGPU sampler",
                        element.name
                    );
                }
                entry.sampler = sampler->handle();
                break;
            }
        }
        entries.push_back(entry);
    }

    WGPUBindGroupDescriptor descriptor {};
    descriptor.label = {desc.name.c_str(), desc.name.size()};
    descriptor.layout = layout->handle();
    descriptor.entryCount = entries.size();
    descriptor.entries = entries.data();
    wgpuDevicePushErrorScope(m_state->device(), WGPUErrorFilter_Validation);
    m_group = wgpuDeviceCreateBindGroup(m_state->device(), &descriptor);
    check_webgpu_error_scope(
        *m_state,
        "WebGPU bind group creation for '" + desc.name + "'"
    );
    if (m_group == nullptr) {
        fatal("Failed to create WebGPU bind group '{}'", desc.name);
    }
}

ResourceSetWebGpu::~ResourceSetWebGpu() {
    if (m_group != nullptr) {
        wgpuBindGroupRelease(m_group);
    }
}

PipelineWebGpu::PipelineWebGpu(
    std::shared_ptr<WebGpuDeviceState> state,
    const RenderPipelineDescription& desc
) : m_state(std::move(state)) {
    m_layout = create_pipeline_layout(m_state->device(), desc.resource_layouts);

    auto vertex_shader = find_shader(desc.shader_program, ShaderStages::Vertex);
    if (!vertex_shader) {
        fatal("WebGPU render pipeline requires a vertex shader");
    }
    auto fragment_shader =
        find_shader(desc.shader_program, ShaderStages::Fragment);

    std::vector<std::vector<WGPUVertexAttribute>> attribute_storage;
    std::vector<WGPUVertexBufferLayout> vertex_buffers;
    attribute_storage.reserve(desc.shader_program.vertex_layouts.size());
    vertex_buffers.reserve(desc.shader_program.vertex_layouts.size());
    for (const auto& layout : desc.shader_program.vertex_layouts) {
        auto& attributes = attribute_storage.emplace_back();
        attributes.reserve(layout.attributes.size());
        for (const auto& attribute : layout.attributes) {
            attributes.push_back(
                WGPUVertexAttribute {
                    .format = to_webgpu(attribute.format, attribute.normalized),
                    .offset = attribute.offset,
                    .shaderLocation =
                        static_cast<std::uint32_t>(attribute.location),
                }
            );
        }
        vertex_buffers.push_back(
            WGPUVertexBufferLayout {
                .stepMode = WGPUVertexStepMode_Vertex,
                .arrayStride = layout.stride,
                .attributeCount = attributes.size(),
                .attributes = attributes.data(),
            }
        );
    }

    std::vector<WGPUBlendState> blend_states(
        desc.output_description.color_attachments.size()
    );
    std::vector<WGPUColorTargetState> color_targets;
    color_targets.reserve(desc.output_description.color_attachments.size());
    for (std::size_t index = 0;
         index < desc.output_description.color_attachments.size();
         ++index) {
        const auto blend = index < desc.blend_state.attachment_states.size() ?
                               desc.blend_state.attachment_states[index] :
                               BlendAttachmentDescription::Disabled;
        auto& webgpu_blend = blend_states[index];
        webgpu_blend.color = {
            .operation = blend_operation(blend.color_function),
            .srcFactor = blend_factor(blend.source_color_factor),
            .dstFactor = blend_factor(blend.destination_color_factor),
        };
        webgpu_blend.alpha = {
            .operation = blend_operation(blend.alpha_function),
            .srcFactor = blend_factor(blend.source_alpha_factor),
            .dstFactor = blend_factor(blend.destination_alpha_factor),
        };
        color_targets.push_back(
            WGPUColorTargetState {
                .format = to_webgpu(
                    desc.output_description.color_attachments[index].format
                ),
                .blend = blend.enabled ? &webgpu_blend : nullptr,
                .writeMask = color_write_mask(blend.color_write_mask),
            }
        );
    }

    WGPUFragmentState fragment {};
    if (fragment_shader) {
        fragment.module = fragment_shader->handle();
        fragment.entryPoint = fragment_shader->entry_point();
        fragment.targetCount = color_targets.size();
        fragment.targets = color_targets.data();
    }

    WGPUDepthStencilState depth_stencil {};
    if (desc.output_description.depth_stencil_attachment) {
        depth_stencil.format =
            to_webgpu(desc.output_description.depth_stencil_attachment->format);
        depth_stencil.depthWriteEnabled =
            desc.depth_stencil_state.depth_write_enabled ?
                WGPUOptionalBool_True :
                WGPUOptionalBool_False;
        depth_stencil.depthCompare =
            desc.depth_stencil_state.depth_test_enabled ?
                to_webgpu(desc.depth_stencil_state.depth_comparison) :
                WGPUCompareFunction_Always;
        depth_stencil.stencilFront.compare = WGPUCompareFunction_Always;
        depth_stencil.stencilFront.failOp = WGPUStencilOperation_Keep;
        depth_stencil.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
        depth_stencil.stencilFront.passOp = WGPUStencilOperation_Keep;
        depth_stencil.stencilBack = depth_stencil.stencilFront;
        depth_stencil.stencilReadMask =
            std::numeric_limits<std::uint32_t>::max();
        depth_stencil.stencilWriteMask =
            std::numeric_limits<std::uint32_t>::max();
    }

    WGPURenderPipelineDescriptor descriptor {};
    descriptor.label = {"fei render pipeline", WGPU_STRLEN};
    descriptor.layout = m_layout;
    descriptor.vertex.module = vertex_shader->handle();
    descriptor.vertex.entryPoint = vertex_shader->entry_point();
    descriptor.vertex.bufferCount = vertex_buffers.size();
    descriptor.vertex.buffers = vertex_buffers.data();
    descriptor.primitive.topology = primitive_topology(desc.render_primitive);
    descriptor.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    // WebGPU determines front-facing orientation after its Y-down viewport
    // transform, which reverses the engine's clip-space winding convention.
    descriptor.primitive.frontFace =
        desc.rasterizer_state.front_face == FrontFace::Clockwise ?
            WGPUFrontFace_CCW :
            WGPUFrontFace_CW;
    descriptor.primitive.cullMode = cull_mode(desc.rasterizer_state.cull_mode);
    descriptor.primitive.unclippedDepth =
        !desc.rasterizer_state.depth_clip_enabled;
    descriptor.depthStencil = desc.output_description.depth_stencil_attachment ?
                                  &depth_stencil :
                                  nullptr;
    descriptor.multisample.count =
        static_cast<std::uint32_t>(desc.output_description.sample_count);
    descriptor.multisample.mask = std::numeric_limits<std::uint32_t>::max();
    descriptor.fragment = fragment_shader ? &fragment : nullptr;

    wgpuDevicePushErrorScope(m_state->device(), WGPUErrorFilter_Validation);
    m_render_pipeline =
        wgpuDeviceCreateRenderPipeline(m_state->device(), &descriptor);
    auto operation =
        "WebGPU render pipeline creation (vertex: " + vertex_shader->path();
    if (fragment_shader) {
        operation += ", fragment: " + fragment_shader->path();
    }
    operation += ')';
    check_webgpu_error_scope(*m_state, operation);
    if (m_render_pipeline == nullptr) {
        fatal("Failed to create WebGPU render pipeline");
    }
}

PipelineWebGpu::PipelineWebGpu(
    std::shared_ptr<WebGpuDeviceState> state,
    const ComputePipelineDescription& desc
) : m_state(std::move(state)) {
    auto shader =
        std::dynamic_pointer_cast<const ShaderModuleWebGpu>(desc.shader);
    if (!shader) {
        fatal("WebGPU compute pipeline received a shader from another backend");
    }
    WGPUComputePipelineDescriptor descriptor {};
    descriptor.label = {"fei compute pipeline", WGPU_STRLEN};
    descriptor.layout = nullptr;
    descriptor.compute.module = shader->handle();
    descriptor.compute.entryPoint = shader->entry_point();
    wgpuDevicePushErrorScope(m_state->device(), WGPUErrorFilter_Validation);
    m_compute_pipeline =
        wgpuDeviceCreateComputePipeline(m_state->device(), &descriptor);
    check_webgpu_error_scope(*m_state, "WebGPU compute pipeline creation");
    if (m_compute_pipeline == nullptr) {
        fatal("Failed to create WebGPU compute pipeline");
    }
    for (std::size_t index = 0; index < desc.resource_layouts.size(); ++index) {
        auto layout = std::dynamic_pointer_cast<const ResourceLayoutWebGpu>(
            desc.resource_layouts[index]
        );
        if (!layout) {
            fatal(
                "WebGPU compute pipeline received a layout from another "
                "backend"
            );
        }
        layout->adopt_pipeline_layout(wgpuComputePipelineGetBindGroupLayout(
            m_compute_pipeline,
            static_cast<std::uint32_t>(index)
        ));
    }
}

PipelineWebGpu::~PipelineWebGpu() {
    if (m_render_pipeline != nullptr) {
        wgpuRenderPipelineRelease(m_render_pipeline);
    }
    if (m_compute_pipeline != nullptr) {
        wgpuComputePipelineRelease(m_compute_pipeline);
    }
    if (m_layout != nullptr) {
        wgpuPipelineLayoutRelease(m_layout);
    }
}

} // namespace fei
