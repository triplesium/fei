#version 450 core
#extension GL_ARB_shader_image_load_store : require

in GeometryOut
{
    vec3 wsPosition;
    vec3 position;
    vec3 normal;
    vec2 texCoord;
    flat vec4 triangleAABB;
} In;

layout (location = 0) out vec4 fragColor;
layout (pixel_center_integer) in vec4 gl_FragCoord;

layout(r32ui) uniform volatile coherent uimage3D voxel_albedo;
layout(r32ui) uniform volatile coherent uimage3D voxel_normal;
layout(r32ui) uniform volatile coherent uimage3D voxel_emissive;
layout(r8) uniform image3D static_voxel_flag;

uniform sampler2D albedo_map;
// uniform sampler2D opacityMap;
uniform sampler2D emissive_map;

layout(row_major, std140) uniform Material {
    vec3 albedo;
    float metallic;
    float roughness;
    vec3 emissive;
    vec3 specular;
    int flags;
} material;

layout(row_major, std140) uniform VxgiVoxelization {
    mat4 view_projections[3];
    mat4 inv_view_projections[3];
    uint volume_dimension;
    uint flag_static_voxels;
    float voxel_scale;
    float voxel_size;
    vec3 world_min_point;
};

vec4 convRGBA8ToVec4(uint val)
{
    return vec4(float((val & 0x000000FF)), 
    float((val & 0x0000FF00) >> 8U), 
    float((val & 0x00FF0000) >> 16U), 
    float((val & 0xFF000000) >> 24U));
}

uint convVec4ToRGBA8(vec4 val)
{
    return (uint(val.w) & 0x000000FF) << 24U | 
    (uint(val.z) & 0x000000FF) << 16U | 
    (uint(val.y) & 0x000000FF) << 8U | 
    (uint(val.x) & 0x000000FF);
}

void imageAtomicRGBA8Avg(layout(r32ui) volatile coherent uimage3D grid, ivec3 coords, vec4 value)
{
    value.rgb *= 255.0;                 // optimize following calculations
    uint newVal = convVec4ToRGBA8(value);
    uint prevStoredVal = 0;
    uint curStoredVal;
    uint numIterations = 0;

    while((curStoredVal = imageAtomicCompSwap(grid, coords, prevStoredVal, newVal))
            != prevStoredVal
            && numIterations < 255)
    {
        prevStoredVal = curStoredVal;
        vec4 rval = convRGBA8ToVec4(curStoredVal);
        rval.rgb = (rval.rgb * rval.a); // Denormalize
        vec4 curValF = rval + value;    // Add
        curValF.rgb /= curValF.a;       // Renormalize
        newVal = convVec4ToRGBA8(curValF);

        ++numIterations;
    }
}

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
        // average normal per fragments sorrounding the voxel volume
        imageAtomicRGBA8Avg(voxel_normal, position, normal);
        // average albedo per fragments sorrounding the voxel volume
        imageAtomicRGBA8Avg(voxel_albedo, position, albedo);
        // average emission per fragments sorrounding the voxel volume
        imageAtomicRGBA8Avg(voxel_emissive, position, emissive);
        // doing a static flagging pass for static geometry voxelization
        if(flag_static_voxels == 1)
        {
            imageStore(static_voxel_flag, position, vec4(1.0));
        }
    }
}
