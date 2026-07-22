public struct MeshVertexInput {
    public [vk::location(0)] float3 position : POSITION;
#ifdef VERTEX_NORMALS
    public [vk::location(1)] float3 normal : NORMAL;
#endif
#ifdef VERTEX_UVS
    public [vk::location(2)] float2 uv : TEXCOORD0;
#endif
#ifdef VERTEX_UVS_1
    public [vk::location(3)] float2 uv1 : TEXCOORD1;
#endif
#ifdef VERTEX_TANGENTS
    public [vk::location(4)] float3 tangent : TANGENT;
#endif
#ifdef VERTEX_COLORS
    public [vk::location(5)] float4 color : COLOR0;
#endif
};
