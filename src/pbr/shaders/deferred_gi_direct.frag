#version 450 core

layout(location = 0) out vec4 fragColor;

layout(location = 0) in vec2 Frag_TexCoords;

layout(set = 1, binding = 0) uniform sampler2D g_position_ao;
layout(set = 1, binding = 1) uniform sampler2D g_normal_roughness;
layout(set = 1, binding = 2) uniform sampler2D g_albedo_metallic;
layout(set = 1, binding = 3) uniform sampler2D g_specular;
layout(set = 1, binding = 4) uniform sampler2D g_emissive_depth;

layout(set = 2, binding = 1) uniform sampler2D shadow_map;

const float PI = 3.14159265f;
const float HALF_PI = 1.57079f;
const float EPSILON = 1e-30;
const float SQRT_3 = 1.73205080f;
const uint MAX_DIRECTIONAL_LIGHTS = 3;
const uint MAX_POINT_LIGHTS = 6;
const uint MAX_SPOT_LIGHTS = 6;

layout(set = 0, binding = 0, row_major, std140) uniform View {
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

layout(set = 2, binding = 0, row_major, std140) uniform Lighting {
    Light directional_lights[MAX_DIRECTIONAL_LIGHTS];
    Light point_lights[MAX_POINT_LIGHTS];
    Light spot_lights[MAX_SPOT_LIGHTS];
    uint num_directional_lights;
    uint num_point_lights;
    uint num_spot_lights;
    mat4 light_view_projection;
};

const vec2 exponents = vec2(40.0f, 5.0f);
const float lightBleedingReduction = 0.0f;

float linstep(float low, float high, float value)
{
    return clamp((value - low) / (high - low), 0.0f, 1.0f);
}  

float ReduceLightBleeding(float pMax, float Amount)  
{  
    return linstep(Amount, 1, pMax);  
} 

vec2 WarpDepth(float depth)
{
    depth = 2.0f * depth - 1.0f;
    float pos = exp(exponents.x * depth);
    float neg = -exp(-exponents.y * depth);
    return vec2(pos, neg);
}

float Chebyshev(vec2 moments, float mean, float minVariance)
{
    if(mean <= moments.x)
    {
        return 1.0f;
    }
    else
    {
        float variance = moments.y - (moments.x * moments.x);
        variance = max(variance, minVariance);
        float d = mean - moments.x;
        float lit = variance / (variance + (d * d));
        return ReduceLightBleeding(lit, lightBleedingReduction);
    }
}

float Visibility(vec3 position)
{
    vec4 lsPos = light_view_projection * vec4(position, 1.0f);
    // avoid arithmetic error
    if(abs(lsPos.w) <= EPSILON) return 1.0f;
    // transform to ndc-space [-1, 1]
    lsPos /= lsPos.w;
    // outside shadow frustum depth range should not be shadowed
    if(lsPos.z < -1.0f || lsPos.z > 1.0f) return 1.0f;
    // convert xy from ndc to uv [0, 1]
    vec2 uv = lsPos.xy * 0.5f + 0.5f;
    // outside map footprint fallback
    if(any(lessThan(uv, vec2(0.0f))) || any(greaterThan(uv, vec2(1.0f)))) return 1.0f;
    // query visibility
    vec4 moments = texture(shadow_map, uv);
    // empty texels are treated as fully lit
    if(dot(abs(moments), vec4(1.0f)) <= EPSILON) return 1.0f;
    // convert z from ndc to depth [0, 1]
    float depth = lsPos.z * 0.5f + 0.5f;
    // move to avoid acne
    vec2 wDepth = WarpDepth(depth - 0.0001f);
    // derivative of warping at depth
    vec2 depthScale = 0.0001f * exponents * wDepth;
    vec2 minVariance = depthScale * depthScale;
    // evsm mode 4 compares negative and positive
    float positive = Chebyshev(moments.xz, wDepth.x, minVariance.x);
    float negative = Chebyshev(moments.yw, wDepth.y, minVariance.y);
    // shadowing value
    return min(positive, negative);
}

vec3 Ambient(Light light, vec3 albedo)
{
    return max(albedo * light.ambient, 0.0f);
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(N, H), 0.0f);
    float nDotH2 = nDotH * nDotH;

    float numerator = a2;
    float denominator = (nDotH2 * (a2 - 1.0f) + 1.0f);
    denominator = PI * denominator * denominator;

    return numerator / max(denominator, EPSILON);
}

float GeometrySchlickGGX(float nDotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;

    float numerator = nDotV;
    float denominator = nDotV * (1.0f - k) + k;
    return numerator / max(denominator, EPSILON);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float nDotV = max(dot(N, V), 0.0f);
    float nDotL = max(dot(N, L), 0.0f);
    float ggx2 = GeometrySchlickGGX(nDotV, roughness);
    float ggx1 = GeometrySchlickGGX(nDotL, roughness);
    return ggx1 * ggx2;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

vec3 BRDF(
    vec3 N,
    vec3 V,
    vec3 L,
    vec3 radiance,
    vec3 albedo,
    float metallic,
    float roughness,
    vec3 F0)
{
    vec3 H = normalize(V + L);
    float nDotL = max(dot(N, L), 0.0f);
    float nDotV = max(dot(N, V), 0.0f);
    if(nDotL <= 0.0f || nDotV <= 0.0f)
    {
        return vec3(0.0f);
    }

    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = FresnelSchlick(clamp(dot(H, V), 0.0f, 1.0f), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0f * nDotV * nDotL + 0.001f;
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = (vec3(1.0f) - kS) * (1.0f - metallic);
    vec3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * radiance * nDotL;
}

vec3 CalculateDirectional(Light light, vec3 normal, vec3 viewDir, vec3 position, vec3 albedo, float metallic, float roughness, vec3 F0)
{
    float visibility = 1.0f;

    // if(light.shadowingMethod == 1)
    // {
        visibility = Visibility(position);
    // }
    // else if(light.shadowingMethod == 2)
    // {
    //     visibility = max(0.0f, TraceShadowCone(position, light.direction, cone_shadow_aperture, 1.0f / voxel_scale));
    // }
    // else if(light.shadowingMethod == 3)
    // {
    //     vec3 voxelPos = WorldToVoxel(position);  
    //     visibility = max(0.0f, texture(voxel_visibility, voxelPos).a);
    // }

    if(visibility <= 0.0f) return vec3(0.0f);  

    vec3 lightDir = normalize(light.direction);
    vec3 radiance = max(light.diffuse, vec3(0.0f));
    return BRDF(normal, viewDir, lightDir, radiance, albedo, metallic, roughness, F0) * visibility;
}

vec3 CalculatePoint(Light light, vec3 normal, vec3 viewDir, vec3 position, vec3 albedo, float metallic, float roughness, vec3 F0)
{
    vec3 lightDir = light.position - position;
    float d = length(lightDir);
    lightDir = normalize(lightDir);
    float falloff = 1.0f / (light.attenuation.constant + light.attenuation.linear * d
                    + light.attenuation.quadratic * d * d + 1.0f);

    if(falloff <= 0.0f) return vec3(0.0f);

    float visibility = 1.0f;

    if(visibility <= 0.0f) return vec3(0.0f);  

    vec3 radiance = max(light.diffuse, vec3(0.0f)) * falloff;
    return BRDF(normal, viewDir, lightDir, radiance, albedo, metallic, roughness, F0) * visibility;
}

vec3 CalculateSpot(Light light, vec3 normal, vec3 viewDir, vec3 position, vec3 albedo, float metallic, float roughness, vec3 F0)
{
    vec3 spotDirection = light.direction;
    vec3 lightDir = normalize(light.position - position);
    float cosAngle = dot(-lightDir, spotDirection);

    // outside the cone
    if(cosAngle < light.angleOuterCone) { return vec3(0.0f); }

    // assuming they are passed as cos(angle)
    float innerMinusOuter = light.angleInnerCone - light.angleOuterCone;
    // spot light factor for smooth transition
    float spotMark = (cosAngle - light.angleOuterCone) / innerMinusOuter;
    float spotFalloff = smoothstep(0.0f, 1.0f, spotMark);

    if(spotFalloff <= 0.0f) return vec3(0.0f);   

    float dst = distance(light.position, position);
    float falloff = 1.0f / (light.attenuation.constant + light.attenuation.linear * dst
                    + light.attenuation.quadratic * dst * dst + 1.0f);   

    if(falloff <= 0.0f) return vec3(0.0f);

    float visibility = 1.0f;


    if(visibility <= 0.0f) return vec3(0.0f); 

    vec3 radiance = max(light.diffuse, vec3(0.0f)) * falloff * spotFalloff;
    return BRDF(normal, viewDir, lightDir, radiance, albedo, metallic, roughness, F0) * visibility;
}

vec3 CalculateDirectLighting(vec3 position, vec3 normal, vec3 albedo, float metallic, float roughness, vec3 F0)
{
    // calculate directional lighting
    vec3 directLighting = vec3(0.0f);
    vec3 viewDir = normalize(view.world_position - position);

    // calculate lighting for directional lights
    for(int i = 0; i < num_directional_lights; ++i)
    {
        directLighting += CalculateDirectional(
            directional_lights[i], normal, viewDir, position, albedo, metallic, roughness, F0);
    }

    // calculate lighting for point lights
    for(int i = 0; i < num_point_lights; ++i)
    {
        directLighting += CalculatePoint(
            point_lights[i], normal, viewDir, position, albedo, metallic, roughness, F0);
    }

    // calculate lighting for spot lights
    for(int i = 0; i < num_spot_lights; ++i) 
    {
        directLighting += CalculateSpot(
            spot_lights[i], normal, viewDir, position, albedo, metallic, roughness, F0);
    }

    return directLighting;
}

void main()
{
    vec3 position = texture(g_position_ao, Frag_TexCoords).rgb;
    vec3 normal = normalize(texture(g_normal_roughness, Frag_TexCoords).xyz);
    float roughness = clamp(texture(g_normal_roughness, Frag_TexCoords).a, 0.045f, 1.0f);
    vec3 baseColor = texture(g_albedo_metallic, Frag_TexCoords).rgb;
    float metallic = clamp(texture(g_albedo_metallic, Frag_TexCoords).a, 0.0f, 1.0f);
    vec3 albedo = pow(baseColor, vec3(2.2f));
    vec3 specularColor = texture(g_specular, Frag_TexCoords).rgb;
    vec3 directLighting = vec3(1.0f);
    vec3 F0 = mix(vec3(0.04f), albedo, metallic);
    // Keep compatibility with the existing specular buffer as a reflectance override.
    F0 = mix(F0, clamp(specularColor, 0.0f, 1.0f), 0.5f);

    directLighting = CalculateDirectLighting(position, normal, albedo, metallic, roughness, F0);

    fragColor = vec4(directLighting, 1.0f);
}
