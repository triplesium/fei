#version 430

layout(location = 0) out vec4 fragColor;

in vec2 Frag_TexCoords;

uniform sampler2D g_position_ao;
uniform sampler2D g_normal_roughness;
uniform sampler2D g_albedo_metallic;
uniform sampler2D g_specular;
uniform sampler2D g_emissive_depth;

uniform sampler2D shadow_map;
uniform sampler3D voxel_visibility;

uniform sampler3D voxel_tex;
uniform sampler3D voxel_tex_mipmap[6];

const float PI = 3.14159265f;
const float HALF_PI = 1.57079f;
const float EPSILON = 1e-30;
const float SQRT_3 = 1.73205080f;
const uint MAX_DIRECTIONAL_LIGHTS = 3;
const uint MAX_POINT_LIGHTS = 6;
const uint MAX_SPOT_LIGHTS = 6;

layout(row_major, std140) uniform View {
    mat4 clip_from_world;
    mat4 view_from_world;
    mat4 clip_from_view;
    vec3 world_position;
} view;

struct Attenuation {
    float constant;
    float linear;
    float quadratic;
};

struct Light {
    float angleInnerCone;
    float angleOuterCone;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;

    vec3 position;
    vec3 direction;

    uint shadowingMethod;
    Attenuation attenuation;
};

layout(row_major, std140) uniform Vxgi {
    Light directional_lights[MAX_DIRECTIONAL_LIGHTS];
    Light point_lights[MAX_POINT_LIGHTS];
    Light spot_lights[MAX_SPOT_LIGHTS];
    uint num_directional_lights;
    uint num_point_lights;
    uint num_spot_lights;

    float voxel_scale;
    vec3 world_min_point;
    vec3 world_max_point;
    int volume_dimension;

    float max_tracing_distance_global;
    float bounce_strength;
    float ao_falloff;
    float ao_alpha;
    float sampling_factor;
    float cone_shadow_tolerance;
    float cone_shadow_aperture;
    uint mode;
    mat4 light_view_projection;
};

const vec2 exponents = vec2(40.0f, 5.0f);
const float lightBleedingReduction = 0.0f;

// uniform Light directionalLight[MAX_DIRECTIONAL_LIGHTS];
// uniform Light pointLight[MAX_POINT_LIGHTS];
// uniform Light spotLight[MAX_SPOT_LIGHTS];
// uniform uint lightTypeCount[3];

// uniform float voxelScale;
// uniform vec3 worldMinPoint;
// uniform vec3 worldMaxPoint;
// uniform int volumeDimension;

// uniform float maxTracingDistanceGlobal = 1.0f;
// uniform float bounceStrength = 1.0f;
// uniform float aoFalloff = 725.0f;
// uniform float aoAlpha = 0.01f;
// uniform float samplingFactor = 0.5f;
// uniform float coneShadowTolerance = 1.0f;
// uniform float coneShadowAperture = 0.03f;
// uniform uint mode = 0;

const vec3 diffuseConeDirections[] =
{
    vec3(0.0f, 1.0f, 0.0f),
    vec3(0.0f, 0.5f, 0.866025f),
    vec3(0.823639f, 0.5f, 0.267617f),
    vec3(0.509037f, 0.5f, -0.7006629f),
    vec3(-0.50937f, 0.5f, -0.7006629f),
    vec3(-0.823639f, 0.5f, 0.267617f)
};

const float diffuseConeWeights[] =
{
    PI / 4.0f,
    3.0f * PI / 20.0f,
    3.0f * PI / 20.0f,
    3.0f * PI / 20.0f,
    3.0f * PI / 20.0f,
    3.0f * PI / 20.0f,
};

vec3 WorldToVoxel(vec3 position)
{
    vec3 voxelPos = position - world_min_point;
    return voxelPos * voxel_scale;
}

vec4 AnistropicSample(vec3 coord, vec3 weight, uvec3 face, float lod)
{
    // anisotropic volumes level
    float anisoLevel = max(lod - 1.0f, 0.0f);
    // directional sample
    vec4 anisoSample = weight.x * textureLod(voxel_tex_mipmap[face.x], coord, anisoLevel)
                     + weight.y * textureLod(voxel_tex_mipmap[face.y], coord, anisoLevel)
                     + weight.z * textureLod(voxel_tex_mipmap[face.z], coord, anisoLevel);
    // linearly interpolate on base level
    if(lod < 1.0f)
    {
        vec4 baseColor = texture(voxel_tex, coord);
        anisoSample = mix(baseColor, anisoSample, clamp(lod, 0.0f, 1.0f));
    }

    return anisoSample;                    
}

bool IntersectRayWithWorldAABB(vec3 ro, vec3 rd, out float enter, out float leave)
{
    vec3 tempMin = (world_min_point - ro) / rd; 
    vec3 tempMax = (world_max_point - ro) / rd;
    
    vec3 v3Max = max (tempMax, tempMin);
    vec3 v3Min = min (tempMax, tempMin);
    
    leave = min (v3Max.x, min (v3Max.y, v3Max.z));
    enter = max (max (v3Min.x, 0.0), max (v3Min.y, v3Min.z));    
    
    return leave > enter;
}

vec4 TraceCone(vec3 position, vec3 normal, vec3 direction, float aperture, bool traceOcclusion)
{
    uvec3 visibleFace;
    visibleFace.x = (direction.x < 0.0) ? 0 : 1;
    visibleFace.y = (direction.y < 0.0) ? 2 : 3;
    visibleFace.z = (direction.z < 0.0) ? 4 : 5;
    traceOcclusion = traceOcclusion && ao_alpha < 1.0f;
    // world space grid voxel size
    float voxelWorldSize = 2.0 /  (voxel_scale * volume_dimension);
    // weight per axis for aniso sampling
    vec3 weight = direction * direction;
    // move further to avoid self collision
    float dst = voxelWorldSize;
    vec3 startPosition = position + normal * dst;
    // final results
    vec4 coneSample = vec4(0.0f);
    float occlusion = 0.0f;
    float maxDistance = max_tracing_distance_global * (1.0f / voxel_scale);
    float falloff = 0.5f * ao_falloff * voxel_scale;
    // out of boundaries check
    float enter = 0.0; float leave = 0.0;

    if(!IntersectRayWithWorldAABB(position, direction, enter, leave))
    {
        coneSample.a = 1.0f;
    }

    while(coneSample.a < 1.0f && dst <= maxDistance)
    {
        vec3 conePosition = startPosition + direction * dst;
        // cone expansion and respective mip level based on diameter
        float diameter = 2.0f * aperture * dst;
        float mipLevel = log2(diameter / voxelWorldSize);
        // convert position to texture coord
        vec3 coord = WorldToVoxel(conePosition);
        // get directional sample from anisotropic representation
        vec4 anisoSample = AnistropicSample(coord, weight, visibleFace, mipLevel);
        // front to back composition
        coneSample += (1.0f - coneSample.a) * anisoSample;
        // ambient occlusion
        if(traceOcclusion && occlusion < 1.0)
        {
            occlusion += ((1.0f - occlusion) * anisoSample.a) / (1.0f + falloff * diameter);
        }
        // move further into volume
        dst += diameter * sampling_factor;
    }

    return vec4(coneSample.rgb, occlusion);
}

vec4 CalculateIndirectLighting(vec3 position, vec3 normal, vec3 albedo, vec4 specular, bool ambientOcclusion)
{
    vec4 specularTrace = vec4(0.0f);
    vec4 diffuseTrace = vec4(0.0f);
    vec3 coneDirection = vec3(0.0f);

    // component greater than zero
    if(any(greaterThan(specular.rgb, specularTrace.rgb)))
    {
        vec3 viewDirection = normalize(view.world_position - position);
        vec3 coneDirection = reflect(-viewDirection, normal);
        coneDirection = normalize(coneDirection);
        // specular cone setup, minimum of 1 grad, fewer can severly slow down performance
        float aperture = clamp(tan(HALF_PI * (1.0f - specular.a)), 0.0174533f, PI);
        specularTrace = TraceCone(position, normal, coneDirection, aperture, false);
        specularTrace.rgb *= specular.rgb;
    }

    // component greater than zero
    if(any(greaterThan(albedo, diffuseTrace.rgb)))
    {
        // diffuse cone setup
        const float aperture = 0.57735f;
        vec3 guide = vec3(0.0f, 1.0f, 0.0f);

        if (abs(dot(normal,guide)) == 1.0f)
        {
            guide = vec3(0.0f, 0.0f, 1.0f);
        }

        // Find a tangent and a bitangent
        vec3 right = normalize(guide - dot(normal, guide) * normal);
        vec3 up = cross(right, normal);

        for(int i = 0; i < 6; i++)
        {
            coneDirection = normal;
            coneDirection += diffuseConeDirections[i].x * right + diffuseConeDirections[i].z * up;
            coneDirection = normalize(coneDirection);
            // cumulative result
            diffuseTrace += TraceCone(position, normal, coneDirection, aperture, ambientOcclusion) * diffuseConeWeights[i];
        }

        diffuseTrace.rgb *= albedo;
    }

    vec3 result = bounce_strength * (diffuseTrace.rgb + specularTrace.rgb);

    return vec4(result, ambientOcclusion ? clamp(1.0f - diffuseTrace.a + ao_alpha, 0.0f, 1.0f) : 1.0f);
}

void main()
{
    vec3 position = texture(g_position_ao, Frag_TexCoords).rgb;
    vec3 normal = normalize(texture(g_normal_roughness, Frag_TexCoords).xyz);
    vec4 specular = texture(g_specular, Frag_TexCoords);
    vec3 albedo = texture(g_albedo_metallic, Frag_TexCoords).rgb;
    vec3 emissive = texture(g_emissive_depth, Frag_TexCoords).rgb;

    vec4 indirectLighting = CalculateIndirectLighting(position, normal, albedo, specular, true);

    // convert indirect to linear space
    indirectLighting.rgb = pow(indirectLighting.rgb, vec3(2.2f));
    fragColor = indirectLighting;
}
