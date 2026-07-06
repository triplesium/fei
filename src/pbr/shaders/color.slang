#include "common.slangh"

struct FragmentInput {
    [vk::location(0)] float2 tex_coords : TEXCOORD0;
};

layout(set = 2, binding = 0) ConstantBuffer<SimpleMaterialData> Material;

[shader("fragment")]
float4 fragment_main(FragmentInput input) : SV_Target0 {
    return float4(Material.albedo, 1.0);
}
