#include "mipmap_generator.hpp"

#include "base/log.hpp"
#include "graphics_webgpu/context.hpp"
#include "graphics_webgpu/resources.hpp"
#include "graphics_webgpu/utils.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <utility>

namespace ets {

namespace {

constexpr std::uint32_t WORKGROUP_SIZE = 8;

constexpr const char* MIPMAP_SHADER = R"(
@group(0) @binding(0) var source_mip: texture_2d_array<f32>;
@group(0) @binding(1) var destination_mip: texture_storage_2d_array<rgba32float, write>;

@compute @workgroup_size(8, 8, 1)
fn compute_main(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let destination_size = textureDimensions(destination_mip);
    if (global_id.x >= destination_size.x ||
        global_id.y >= destination_size.y) {
        return;
    }

    let source_size = textureDimensions(source_mip);
    let max_source = vec2<i32>(source_size) - vec2<i32>(1);
    let source_position = vec2<i32>(global_id.xy * 2u);
    let layer = i32(global_id.z);

    var color = vec4<f32>(0.0);
    color += textureLoad(
        source_mip,
        min(source_position, max_source),
        layer,
        0
    );
    color += textureLoad(
        source_mip,
        min(source_position + vec2<i32>(1, 0), max_source),
        layer,
        0
    );
    color += textureLoad(
        source_mip,
        min(source_position + vec2<i32>(0, 1), max_source),
        layer,
        0
    );
    color += textureLoad(
        source_mip,
        min(source_position + vec2<i32>(1, 1), max_source),
        layer,
        0
    );

    textureStore(
        destination_mip,
        vec2<i32>(global_id.xy),
        layer,
        color * 0.25
    );
}
)";

struct MipmapPassResources {
    WGPUTextureView source_view {nullptr};
    WGPUTextureView destination_view {nullptr};
    WGPUBindGroup bind_group {nullptr};

    ~MipmapPassResources() {
        if (bind_group != nullptr) {
            wgpuBindGroupRelease(bind_group);
        }
        if (destination_view != nullptr) {
            wgpuTextureViewRelease(destination_view);
        }
        if (source_view != nullptr) {
            wgpuTextureViewRelease(source_view);
        }
    }
};

std::uint32_t actual_array_layers(const Texture& texture) {
    return texture.usage().is_set(TextureUsage::Cubemap) ? texture.layer() * 6 :
                                                           texture.layer();
}

std::uint32_t mip_dimension(std::uint32_t value, std::uint32_t mip_level) {
    return std::max(value >> mip_level, 1U);
}

WGPUTextureView create_mip_view(
    const TextureWebGpu& texture,
    std::uint32_t mip_level,
    std::uint32_t array_layers,
    WGPUTextureUsage usage,
    const char* label
) {
    WGPUTextureViewDescriptor descriptor {};
    descriptor.label = {label, WGPU_STRLEN};
    descriptor.format = to_webgpu(texture.format());
    descriptor.dimension = WGPUTextureViewDimension_2DArray;
    descriptor.baseMipLevel = mip_level;
    descriptor.mipLevelCount = 1;
    descriptor.baseArrayLayer = 0;
    descriptor.arrayLayerCount = array_layers;
    descriptor.aspect = WGPUTextureAspect_All;
    descriptor.usage = usage;
    auto view = wgpuTextureCreateView(texture.handle(), &descriptor);
    if (view == nullptr) {
        fatal("Failed to create WebGPU mipmap texture view");
    }
    return view;
}

} // namespace

bool supports_webgpu_mipmap_generation(const TextureDescription& desc) {
    return desc.texture_type == TextureType::Texture2D &&
           desc.texture_format == PixelFormat::Rgba32Float &&
           desc.sample_count == TextureSampleCount::Count1;
}

MipmapGeneratorWebGpu::MipmapGeneratorWebGpu(const WebGpuDeviceState& state) :
    m_device(state.device()) {
    std::array<WGPUBindGroupLayoutEntry, 2> entries {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    entries[0].texture.viewDimension = WGPUTextureViewDimension_2DArray;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Compute;
    entries[1].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    entries[1].storageTexture.format = WGPUTextureFormat_RGBA32Float;
    entries[1].storageTexture.viewDimension = WGPUTextureViewDimension_2DArray;

    WGPUBindGroupLayoutDescriptor bind_group_layout_desc {};
    bind_group_layout_desc.label = {
        "entisium mipmap bind group layout",
        WGPU_STRLEN
    };
    bind_group_layout_desc.entryCount = entries.size();
    bind_group_layout_desc.entries = entries.data();
    m_bind_group_layout =
        wgpuDeviceCreateBindGroupLayout(m_device, &bind_group_layout_desc);
    if (m_bind_group_layout == nullptr) {
        fatal("Failed to create WebGPU mipmap bind group layout");
    }

    WGPUPipelineLayoutDescriptor pipeline_layout_desc {};
    pipeline_layout_desc.label = {
        "entisium mipmap pipeline layout",
        WGPU_STRLEN
    };
    pipeline_layout_desc.bindGroupLayoutCount = 1;
    pipeline_layout_desc.bindGroupLayouts = &m_bind_group_layout;
    m_pipeline_layout =
        wgpuDeviceCreatePipelineLayout(m_device, &pipeline_layout_desc);
    if (m_pipeline_layout == nullptr) {
        fatal("Failed to create WebGPU mipmap pipeline layout");
    }

    WGPUShaderSourceWGSL shader_source {
        .chain = {.sType = WGPUSType_ShaderSourceWGSL},
        .code = {MIPMAP_SHADER, WGPU_STRLEN},
    };
    WGPUShaderModuleDescriptor shader_desc {};
    shader_desc.nextInChain = &shader_source.chain;
    shader_desc.label = {"entisium mipmap shader", WGPU_STRLEN};
    push_webgpu_error_scope(state);
    m_shader = wgpuDeviceCreateShaderModule(m_device, &shader_desc);
    check_webgpu_error_scope(state, "WebGPU mipmap shader creation");
    if (m_shader == nullptr) {
        fatal("Failed to create WebGPU mipmap shader");
    }

    WGPUComputePipelineDescriptor pipeline_desc {};
    pipeline_desc.label = {"entisium mipmap pipeline", WGPU_STRLEN};
    pipeline_desc.layout = m_pipeline_layout;
    pipeline_desc.compute.module = m_shader;
    pipeline_desc.compute.entryPoint = {"compute_main", WGPU_STRLEN};
    push_webgpu_error_scope(state);
    m_pipeline = wgpuDeviceCreateComputePipeline(m_device, &pipeline_desc);
    check_webgpu_error_scope(state, "WebGPU mipmap pipeline creation");
    if (m_pipeline == nullptr) {
        fatal("Failed to create WebGPU mipmap pipeline");
    }
}

MipmapGeneratorWebGpu::~MipmapGeneratorWebGpu() {
    if (m_pipeline != nullptr) {
        wgpuComputePipelineRelease(m_pipeline);
    }
    if (m_pipeline_layout != nullptr) {
        wgpuPipelineLayoutRelease(m_pipeline_layout);
    }
    if (m_bind_group_layout != nullptr) {
        wgpuBindGroupLayoutRelease(m_bind_group_layout);
    }
    if (m_shader != nullptr) {
        wgpuShaderModuleRelease(m_shader);
    }
}

void MipmapGeneratorWebGpu::encode(
    WGPUCommandEncoder encoder,
    const TextureWebGpu& texture,
    std::vector<std::shared_ptr<const void>>& retained_resources
) const {
    const auto array_layers = actual_array_layers(texture);
    for (std::uint32_t mip_level = 1; mip_level < texture.mip_level();
         ++mip_level) {
        auto resources = std::make_shared<MipmapPassResources>();
        resources->source_view = create_mip_view(
            texture,
            mip_level - 1,
            array_layers,
            WGPUTextureUsage_TextureBinding,
            "entisium mipmap source view"
        );
        resources->destination_view = create_mip_view(
            texture,
            mip_level,
            array_layers,
            WGPUTextureUsage_StorageBinding,
            "entisium mipmap destination view"
        );

        std::array<WGPUBindGroupEntry, 2> entries {};
        entries[0].binding = 0;
        entries[0].textureView = resources->source_view;
        entries[1].binding = 1;
        entries[1].textureView = resources->destination_view;
        WGPUBindGroupDescriptor bind_group_desc {};
        bind_group_desc.label = {"entisium mipmap bind group", WGPU_STRLEN};
        bind_group_desc.layout = m_bind_group_layout;
        bind_group_desc.entryCount = entries.size();
        bind_group_desc.entries = entries.data();
        resources->bind_group =
            wgpuDeviceCreateBindGroup(m_device, &bind_group_desc);
        if (resources->bind_group == nullptr) {
            fatal("Failed to create WebGPU mipmap bind group");
        }

        WGPUComputePassDescriptor pass_desc {};
        pass_desc.label = {"entisium mipmap pass", WGPU_STRLEN};
        auto pass = wgpuCommandEncoderBeginComputePass(encoder, &pass_desc);
        if (pass == nullptr) {
            fatal("Failed to begin WebGPU mipmap compute pass");
        }
        wgpuComputePassEncoderSetPipeline(pass, m_pipeline);
        wgpuComputePassEncoderSetBindGroup(
            pass,
            0,
            resources->bind_group,
            0,
            nullptr
        );
        const auto width = mip_dimension(texture.width(), mip_level);
        const auto height = mip_dimension(texture.height(), mip_level);
        wgpuComputePassEncoderDispatchWorkgroups(
            pass,
            (width + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE,
            (height + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE,
            array_layers
        );
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        retained_resources.push_back(std::move(resources));
    }
}

} // namespace ets
