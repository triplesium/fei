#pragma once

#include "graphics/enums.hpp"
#include "graphics/texture_view.hpp"

#include <webgpu/webgpu.h>

namespace fei {

WGPUTextureFormat to_webgpu(PixelFormat value);
PixelFormat from_webgpu(WGPUTextureFormat value);
WGPUShaderStage to_webgpu_shader_stages(BitFlags<ShaderStages> value);
WGPUVertexFormat to_webgpu(VertexFormat value, bool normalized = false);
WGPUCompareFunction to_webgpu(ComparisonKind value);
WGPUTextureViewDimension to_webgpu(TextureViewType value);

} // namespace fei
