#version 450 core
layout(location = 0) in vec3 Frag_Position;
layout(location = 1) in vec3 Frag_Normal;
layout(location = 2) in vec2 Frag_TexCoords;
// in vec4 Frag_LightSpacePosition;
layout(location = 0) out vec4 Out_Color;

const int STANDARD_MATERIAL_FLAGS_ALBEDO_MAP_BIT = 1 << 0;
const int STANDARD_MATERIAL_FLAGS_NORMAL_MAP_BIT = 1 << 1;
const int STANDARD_MATERIAL_FLAGS_METALLIC_MAP_BIT = 1 << 2;
const int STANDARD_MATERIAL_FLAGS_ROUGHNESS_MAP_BIT = 1 << 3;

layout(set = 2, binding = 0, row_major, std140) uniform Material {
    vec3 albedo;
    float metallic;
    float roughness;
    int flags;
} material;

layout(set = 0, binding = 0, row_major, std140) uniform View {
    mat4 clip_from_world;
    mat4 view_from_world;
    mat4 clip_from_view;
    vec3 world_position;
} view;

layout(set = 1, binding = 0, row_major, std140) uniform Mesh {
    mat4 world_from_local;
} mesh;

layout(set = 2, binding = 1) uniform sampler2D albedo_map;
layout(set = 2, binding = 2) uniform sampler2D normal_map;
layout(set = 2, binding = 3) uniform sampler2D metallic_map;
layout(set = 2, binding = 4) uniform sampler2D roughness_map;
layout(set = 0, binding = 1) uniform samplerCube irradiance_map;
layout(set = 0, binding = 2) uniform samplerCube radiance_map;
layout(set = 0, binding = 4) uniform sampler2D brdf_lut;

#define PI 3.141592653589793

vec3 get_normal_from_map() {
    vec3 tangentNormal = texture(normal_map, Frag_TexCoords).xyz * 2.0 - 1.0;

    vec3 Q1  = dFdx(Frag_Position);
    vec3 Q2  = dFdy(Frag_Position);
    vec2 st1 = dFdx(Frag_TexCoords);
    vec2 st2 = dFdy(Frag_TexCoords);

    vec3 N   = normalize(Frag_Normal);
    vec3 T  = normalize(Q1*st2.t - Q2*st1.t);
    vec3 B  = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    return normalize(TBN * tangentNormal);
}

float distribution_ggx(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

float geometry_schlick_ggx(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometry_schlick_ggx(NdotV, roughness);
    float ggx1 = geometry_schlick_ggx(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnel_schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 fresnel_schlick_roughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec3 albedo = material.albedo;
    if ((material.flags & STANDARD_MATERIAL_FLAGS_ALBEDO_MAP_BIT) != 0) {
        albedo = texture(albedo_map, Frag_TexCoords.xy).rgb;
    }

    float metallic = material.metallic;
    if ((material.flags & STANDARD_MATERIAL_FLAGS_METALLIC_MAP_BIT) != 0) {
        metallic = texture(metallic_map, Frag_TexCoords.xy).r;
    }

    float roughness = material.roughness;
    if ((material.flags & STANDARD_MATERIAL_FLAGS_ROUGHNESS_MAP_BIT) != 0) {
        roughness = texture(roughness_map, Frag_TexCoords.xy).r;
    }
    roughness = clamp(roughness, 0.01, 1.0);

    vec3 N;
    if ((material.flags & STANDARD_MATERIAL_FLAGS_NORMAL_MAP_BIT) != 0) {
        N = get_normal_from_map();
    } else {
        N = normalize(Frag_Normal);
    }

    vec3 V = normalize(view.world_position - Frag_Position);
    vec3 R = reflect(-V, N);

    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);
    
    vec3 lighting = vec3(0.0);
    // {   
    //     vec3 L = normalize(light.world_position - Frag_Position);
    //     vec3 H = normalize(V + L);
    //     float distance = length(light.world_position - Frag_Position);
    //     float attenuation = 1.0 / (distance * distance);
    //     vec3 radiance = light.color * attenuation;
    //     float NDF = distribution_ggx(N, H, roughness);   
    //     float G   = geometry_smith(N, V, L, roughness);      
    //     vec3 F    = fresnel_schlick(clamp(dot(H, V), 0.0, 1.0), F0);
    //     vec3 numerator    = NDF * G * F;
    //     float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001; 
    //     vec3 specular = numerator / denominator;
    //     vec3 kS = F;
    //     vec3 kD = vec3(1.0) - kS;
    //     kD *= 1.0 - metallic;
    //     float NdotL = max(dot(N, L), 0.0);
    //     lighting += (kD * albedo / PI + specular) * radiance * NdotL * 4;
    // }

    vec3 F = fresnel_schlick_roughness(max(dot(N, V), 0.0), F0, roughness);

    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    vec3 irradiance = texture(irradiance_map, N).rgb;
    vec3 diffuse = irradiance * albedo;

    vec3 prefiltered_color = textureLod(radiance_map, R, roughness * float(textureQueryLevels(radiance_map))).rgb;
    vec2 brdf = texture(brdf_lut, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular = prefiltered_color * (F * brdf.x + brdf.y);
    
    vec3 ambient = kD * diffuse + specular;

    // Perform perspective divide
    // vec3 shadow_coord = Frag_LightSpacePosition.xyz / Frag_LightSpacePosition.w;
    // Transform NDC to [0,1] range
    // shadow_coord = shadow_coord * 0.5 + 0.5;

    float visibility = 1.0;
    // float visibility = pcss_shadow(
    //     shadow_map,
    //     vec4(shadow_coord, 1.0),
    //     0.08
    // );

    vec3 color = ambient + lighting * visibility;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

    Out_Color = vec4(color, 1.0);
}
