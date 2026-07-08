import rendering.fullscreen;
import pbr.material.types;

layout(set = 2) ParameterBlock<StandardMaterialResources> material;

[shader("fragment")]
float4 fragment_main(FullscreenFragmentInput input) : SV_Target0 {
    return float4(material.albedo, 1.0);
}
