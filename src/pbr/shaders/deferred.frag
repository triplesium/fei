#version 450 core
out vec4 Out_Color;
in vec2 Frag_TexCoords;

layout(row_major, std140) uniform View {
    mat4 clip_from_world;
    mat4 view_from_world;
    mat4 clip_from_view;
    vec3 world_position;
} view;

layout(row_major, std140) uniform Light {
    vec3 world_position;
    vec3 color;
} light;

uniform sampler2D g_position_ao;
uniform sampler2D g_normal_roughness;
uniform sampler2D g_albedo_metallic;
uniform sampler2D g_specular;
uniform sampler2D g_emissive;

uniform samplerCube irradiance_map;
uniform samplerCube radiance_map;
uniform sampler2D brdf_lut;

#define EPS 1e-3
#define PI 3.141592653589793
#define PI2 6.283185307179586

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

vec3 calculate_ambient(vec3 V, vec3 N, vec3 R, vec3 albedo, float metallic, float roughness, float ao) {    
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    vec3 F = fresnel_schlick_roughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    vec3 irradiance = texture(irradiance_map, N).rgb;
    vec3 diffuse = irradiance * albedo;

    vec3 prefiltered_color = textureLod(radiance_map, R, roughness * float(textureQueryLevels(radiance_map))).rgb;
    vec2 brdf = texture(brdf_lut, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular = prefiltered_color * (F * brdf.x + brdf.y);
    
    vec3 ambient = (kD * diffuse + specular) * ao;
    return ambient;
}

vec3 calculate_direct_light(vec3 V, vec3 N, vec3 P, vec3 albedo, float metallic, float roughness) {
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);

    vec3 lighting = vec3(0.0);

    vec3 L = normalize(light.world_position - P);
    vec3 H = normalize(V + L);
    float distance = length(light.world_position - P);
    // float attenuation = 1.0 / (distance * distance);
    // vec3 radiance = light.color * attenuation;
    vec3 radiance = light.color;
    float NDF = distribution_ggx(N, H, roughness);   
    float G   = geometry_smith(N, V, L, roughness);      
    vec3 F    = fresnel_schlick(clamp(dot(H, V), 0.0, 1.0), F0);
    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001; 
    vec3 specular = numerator / denominator;
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;
    float NdotL = max(dot(N, L), 0.0);
    lighting += (kD * albedo / PI + specular) * radiance * NdotL * 4;

    return lighting;
}

void main() {    
    vec3 position = texture(g_position_ao, Frag_TexCoords).rgb;
    float ao = texture(g_position_ao, Frag_TexCoords).a;
    vec3 normal = texture(g_normal_roughness, Frag_TexCoords).rgb;
    float roughness = texture(g_normal_roughness, Frag_TexCoords).a;
    vec3 albedo = texture(g_albedo_metallic, Frag_TexCoords).rgb;
    float metallic = texture(g_albedo_metallic, Frag_TexCoords).a;

    vec3 V = normalize(view.world_position - position);
    vec3 N = normalize(normal);
    vec3 R = reflect(-V, N);

    vec3 lighting = calculate_direct_light(V, N, position, albedo, metallic, roughness);
    
    // vec3 ambient = calculate_ambient(V, N, R, albedo, metallic, roughness, ao);
    vec3 ambient = vec3(0.5) * albedo * ao * 0;

    float visibility = 1.0;

    vec3 color = ambient + lighting * visibility;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

    Out_Color = vec4(color, 1.0);
}
