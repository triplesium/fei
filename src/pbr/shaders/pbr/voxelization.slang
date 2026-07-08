import rendering.color;
import rendering.normal;
import pbr.material.types;
import rendering.mesh;
import pbr.vxgi.types;

using namespace rendering.color;
using namespace rendering.normal;

struct VertexInput {
    [vk::location(0)] float3 position : POSITION;
    [vk::location(1)] float3 normal : NORMAL;
    [vk::location(2)] float2 uv : TEXCOORD0;
};

struct VertexOutput {
    float4 position : SV_Position;
    [vk::location(0)] float2 tex_coords : TEXCOORD0;
    [vk::location(1)] float3 normal : NORMAL_WS;
};

struct GeometryOutput {
    float4 clip_position : SV_Position;
    [vk::location(0)] float3 ws_position : POSITION_WS;
    [vk::location(1)] float3 position : POSITION;
    [vk::location(2)] float3 normal : NORMAL_WS;
    [vk::location(3)] float2 tex_coords : TEXCOORD0;
    [vk::location(4)] nointerpolation float4 triangle_aabb : TEXCOORD1;
};

layout(set = 1, binding = 0) ConstantBuffer<MeshUniform> Mesh;
layout(set = 2) ParameterBlock<StandardMaterialResources> material;
layout(set = 3, binding = 0, rgba8) RWTexture3D<float4> voxel_albedo;
layout(set = 3, binding = 1, rgba8) RWTexture3D<float4> voxel_normal;
layout(set = 3, binding = 2, rgba8) RWTexture3D<float4> voxel_emissive;
layout(set = 3, binding = 4, r8) RWTexture3D<float4> static_voxel_flag;
layout(set = 4, binding = 0) ConstantBuffer<VxgiVoxelizationUniform> VxgiVoxelization;

int calculate_axis(VertexOutput input[3]) {
    float3 p1 = input[1].position.xyz - input[0].position.xyz;
    float3 p2 = input[2].position.xyz - input[0].position.xyz;
    float3 face_normal = cross(p1, p2);

    float normal_dx = abs(face_normal.x);
    float normal_dy = abs(face_normal.y);
    float normal_dz = abs(face_normal.z);
    if (normal_dx > normal_dy && normal_dx > normal_dz) {
        return 0;
    }
    if (normal_dy > normal_dx && normal_dy > normal_dz) {
        return 1;
    }
    return 2;
}

float4 axis_aligned_bounding_box(float4 positions[3], float2 pixel_diagonal) {
    float4 aabb;
    aabb.xy = min(positions[2].xy, min(positions[1].xy, positions[0].xy));
    aabb.zw = max(positions[2].xy, max(positions[1].xy, positions[0].xy));
    aabb.xy -= pixel_diagonal;
    aabb.zw += pixel_diagonal;
    return aabb;
}

[shader("vertex")]
VertexOutput vertex_main(VertexInput input) {
    VertexOutput output;
    output.position = Mesh.position_ws4(input.position);
    output.normal = Mesh.normal_ws(input.normal);
    output.tex_coords = input.uv;
    return output;
}

[shader("geometry")]
[maxvertexcount(3)]
void geometry_main(triangle VertexOutput input[3], inout TriangleStream<GeometryOutput> stream) {
    float2 tex_coords[3];
    int selected_index = calculate_axis(input);
    float4x4 view_projection = VxgiVoxelization.view_projection(selected_index);

    for (int i = 0; i < 3; i++) {
        tex_coords[i] = input[i].tex_coords;
    }

    float4 positions[3] = {
        mul(view_projection, input[0].position),
        mul(view_projection, input[1].position),
        mul(view_projection, input[2].position),
    };

    float4 triangle_plane;
    triangle_plane.xyz = cross(
        positions[1].xyz - positions[0].xyz,
        positions[2].xyz - positions[0].xyz
    );
    triangle_plane.xyz = normalize(triangle_plane.xyz);
    triangle_plane.w = -dot(positions[0].xyz, triangle_plane.xyz);

    int indices[3] = {0, 1, 2};
    if (dot(triangle_plane.xyz, float3(0.0, 0.0, 1.0)) < 0.0) {
        float4 vertex_temp = positions[2];
        float2 tex_coord_temp = tex_coords[2];
        int index_temp = indices[2];

        positions[2] = positions[1];
        tex_coords[2] = tex_coords[1];
        indices[2] = indices[1];

        positions[1] = vertex_temp;
        tex_coords[1] = tex_coord_temp;
        indices[1] = index_temp;
    }

    float2 half_pixel = VxgiVoxelization.half_pixel;
    if (triangle_plane.z == 0.0) {
        return;
    }

    float4 triangle_aabb = axis_aligned_bounding_box(positions, half_pixel);
    float3 planes[3];
    planes[0] = cross(positions[0].xyw - positions[2].xyw, positions[2].xyw);
    planes[1] = cross(positions[1].xyw - positions[0].xyw, positions[0].xyw);
    planes[2] = cross(positions[2].xyw - positions[1].xyw, positions[1].xyw);
    planes[0].z -= dot(half_pixel, abs(planes[0].xy));
    planes[1].z -= dot(half_pixel, abs(planes[1].xy));
    planes[2].z -= dot(half_pixel, abs(planes[2].xy));

    float3 intersection[3];
    intersection[0] = cross(planes[0], planes[1]);
    intersection[1] = cross(planes[1], planes[2]);
    intersection[2] = cross(planes[2], planes[0]);
    intersection[0] /= intersection[0].z;
    intersection[1] /= intersection[1].z;
    intersection[2] /= intersection[2].z;

    float z[3];
    z[0] = -(intersection[0].x * triangle_plane.x +
             intersection[0].y * triangle_plane.y + triangle_plane.w) /
        triangle_plane.z;
    z[1] = -(intersection[1].x * triangle_plane.x +
             intersection[1].y * triangle_plane.y + triangle_plane.w) /
        triangle_plane.z;
    z[2] = -(intersection[2].x * triangle_plane.x +
             intersection[2].y * triangle_plane.y + triangle_plane.w) /
        triangle_plane.z;
    positions[0].xyz = float3(intersection[0].xy, z[0]);
    positions[1].xyz = float3(intersection[1].xy, z[1]);
    positions[2].xyz = float3(intersection[2].xy, z[2]);

    for (int i = 0; i < 3; i++) {
        positions[i].xy = clamp(positions[i].xy, triangle_aabb.xy, triangle_aabb.zw);
    }

    for (int i = 0; i < 3; ++i) {
        GeometryOutput output;
        output.clip_position = positions[i];
        output.position = positions[i].xyz;
        output.normal = input[indices[i]].normal;
        output.tex_coords = tex_coords[i];
        output.ws_position =
            VxgiVoxelization.world_to_voxel_index(input[indices[i]].position.xyz);
        output.triangle_aabb = triangle_aabb;
        stream.Append(output);
    }

    stream.RestartStrip();
}

[shader("fragment")]
float4 fragment_main(GeometryOutput input) : SV_Target0 {
    if (input.position.x < input.triangle_aabb.x ||
        input.position.y < input.triangle_aabb.y ||
        input.position.x > input.triangle_aabb.z ||
        input.position.y > input.triangle_aabb.w) {
        discard;
    }

    int3 position = int3(input.ws_position);
    float4 albedo = material.sample_albedo_texture(input.tex_coords);
    float opacity = albedo.a;

    if (VxgiVoxelization.flag_static_voxels == 0) {
        bool is_static = static_voxel_flag[position].r > 0.0;
        if (is_static) {
            opacity = 0.0;
        }
    }

    if (opacity > 0.0) {
        albedo.rgb = srgb_to_linear(material.albedo) * srgb_to_linear(albedo.rgb);
        albedo.rgb *= opacity;
        albedo.a = 1.0;

        float4 emissive = material.sample_emissive_texture(input.tex_coords);
        emissive.rgb = srgb_to_linear(emissive.rgb) * srgb_to_linear(material.emissive);
        emissive.a = 1.0;

        voxel_normal[position] = float4(encode(normalize(input.normal)), 1.0);
        voxel_albedo[position] = albedo;
        voxel_emissive[position] = emissive;

        if (VxgiVoxelization.flag_static_voxels == 1) {
            static_voxel_flag[position] = float4(1.0);
        }
    }

    return float4(0.0);
}
