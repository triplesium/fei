import pbr.deferred.gbuffer;
import pbr.lib.direct_lighting;
import pbr.shading.types;
import rendering.fullscreen;
import rendering.view;

using namespace pbr.deferred;
using namespace pbr.direct_lighting;

layout(set = 0, binding = 0) ConstantBuffer<ViewUniform> view;
layout(set = 1) ParameterBlock<GBuffer> gbuffer;
layout(set = 2) ParameterBlock<DirectLighting> lighting;

float3 calculate_direct_lighting(GBufferSurface surface) {
    float3 view_dir = view.view_direction(surface.position);
    ShadingPoint point = surface.shading_point(view_dir);
    return lighting.evaluate(point, surface.material);
}

[shader("fragment")]
float4 fragment_main(FullscreenFragmentInput input) : SV_Target0 {
    GBufferSurface surface = gbuffer.read_lighting_surface(input.tex_coords);
    float3 direct = calculate_direct_lighting(surface);
    return float4(direct, 1.0);
}
