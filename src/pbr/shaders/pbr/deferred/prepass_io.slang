namespace pbr.prepass_io {

public struct VertexOutput {
    public float4 position : SV_Position;
    public [vk::location(0)] float3 position_ws : POSITION_WS;
    public [vk::location(1)] float3 normal_ws : NORMAL_WS;
#ifdef VERTEX_UVS
    public [vk::location(2)] float2 tex_coords : TEXCOORD0;
#endif
#ifdef VERTEX_TANGENTS
    public [vk::location(3)] float3 tangent_ws : TANGENT_WS;
#endif
};

public struct FragmentOutput {
    public [vk::location(0)] float4 position_ao : SV_Target0;
    public [vk::location(1)] float4 normal_roughness : SV_Target1;
    public [vk::location(2)] float4 albedo_metallic : SV_Target2;
    public [vk::location(3)] float4 specular : SV_Target3;
    public [vk::location(4)] float4 emissive_depth : SV_Target4;
};

} // namespace pbr.prepass_io
