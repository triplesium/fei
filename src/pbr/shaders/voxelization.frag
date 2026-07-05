#version 450 core
#extension GL_ARB_shader_image_load_store : require

layout(location = 0) in GeometryOut
{
    vec3 wsPosition;
    vec3 position;
    vec3 normal;
    vec2 texCoord;
    flat vec4 triangleAABB;
} In;

layout(location = 0) out vec4 fragColor;

layout(set = 3, binding = 0, rgba8) uniform image3D voxel_albedo;
layout(set = 3, binding = 1, rgba8) uniform image3D voxel_normal;
layout(set = 3, binding = 2, rgba8) uniform image3D voxel_emissive;
layout(set = 3, binding = 4, r8) uniform image3D static_voxel_flag;

layout(set = 2, binding = 1) uniform sampler2D albedo_map;
// uniform sampler2D opacityMap;
layout(set = 2, binding = 5) uniform sampler2D emissive_map;

layout(set = 2, binding = 0, row_major, std140) uniform Material {
    vec3 albedo;
    float metallic;
    float roughness;
    vec3 emissive;
    vec3 specular;
    int flags;
} material;

layout(set = 4, binding = 0, row_major, std140) uniform VxgiVoxelization {
    mat4 view_projections[3];
    mat4 inv_view_projections[3];
    uint volume_dimension;
    uint flag_static_voxels;
    float voxel_scale;
    float voxel_size;
    vec3 world_min_point;
};

vec3 EncodeNormal(vec3 normal)
{
    return normal * 0.5f + vec3(0.5f);
}

vec3 DecodeNormal(vec3 normal)
{
    return normal * 2.0f - vec3(1.0f);
}

void main()
{
    if( In.position.x < In.triangleAABB.x || In.position.y < In.triangleAABB.y || 
		In.position.x > In.triangleAABB.z || In.position.y > In.triangleAABB.w )
	{
		discard;
	}
    fragColor = vec4(0.0);

    // writing coords position
    ivec3 position = ivec3(In.wsPosition);
    // fragment albedo
    vec4 albedo = texture(albedo_map, In.texCoord.xy);
    // float opacity = min(albedo.a, texture(opacityMap, In.texCoord.xy).r);
    float opacity = albedo.a;

    if(flag_static_voxels == 0)
    {
        bool isStatic = imageLoad(static_voxel_flag, position).r > 0.0f;

        // force condition so writing is canceled
        if(isStatic) opacity = 0.0f;
    }

    // alpha cutoff
    if(opacity > 0.0f)
    {
        // albedo is in srgb space, bring back to linear
        albedo.rgb = material.albedo * albedo.rgb;
        // premultiplied alpha
        albedo.rgb *= opacity;
        albedo.a = 1.0f;
        // emission value
        vec4 emissive = texture(emissive_map, In.texCoord.xy);
        emissive.rgb = emissive.rgb * material.emissive;
        emissive.a = 1.0f;
        // bring normal to 0-1 range
        vec4 normal = vec4(EncodeNormal(normalize(In.normal)), 1.0f);
        imageStore(voxel_normal, position, normal);
        imageStore(voxel_albedo, position, albedo);
        imageStore(voxel_emissive, position, emissive);
        // doing a static flagging pass for static geometry voxelization
        if(flag_static_voxels == 1)
        {
            imageStore(static_voxel_flag, position, vec4(1.0));
        }
    }
}
