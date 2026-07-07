import lib.fullscreen;
import material.types;

layout(set = 2, binding = 0) ConstantBuffer<SimpleMaterialData> Material;

[shader("fragment")]
float4 fragment_main(FullscreenFragmentInput input) : SV_Target0 {
    return float4(Material.albedo, 1.0);
}
