#version 450 core
in vec3 Frag_Position;
in vec3 Frag_Normal;
in vec2 Frag_TexCoords;
in vec4 Frag_LightSpacePosition;
out vec4 Out_Color;

layout(row_major, std140) uniform Material {
    vec3 albedo;
    float metallic;
    float roughness;
};
layout(row_major, std140) uniform View {
    mat4 view_projection;
    vec3 view_position;
};
layout(row_major, std140) uniform Mesh {
    mat4 model;
};
layout(row_major, std140) uniform Light {
    mat4 light_view_projection;
    vec3 light_position;
    vec3 light_color;
};
layout(binding = 1) uniform sampler2D albedo_map;
layout(binding = 2) uniform sampler2D shadow_map;
layout(binding = 3) uniform sampler2D normal_map;
layout(binding = 4) uniform sampler2D metallic_map;
layout(binding = 5) uniform sampler2D roughness_map;

#define SHADOW_MAP_SIZE 2048.0
#define LIGHT_FRUSTUM_SIZE 20.0
#define NUM_SAMPLES 50
#define NUM_RINGS 10
#define SAMPLE_RADIUS 10.0
#define NEAR_PLANE 0.01
#define LIGHT_WORLD_SIZE 0.5
#define LIGHT_SIZE_UV (LIGHT_WORLD_SIZE / LIGHT_FRUSTUM_SIZE)

#define EPS 1e-3
#define PI 3.141592653589793
#define PI2 6.283185307179586

vec3 get_normal_from_map()
{
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

highp float rand_2to1(vec2 uv) { 
    // 0 - 1
	const highp float a = 12.9898, b = 78.233, c = 43758.5453;
	highp float dt = dot(uv.xy, vec2(a, b)), sn = mod(dt, PI);
	return fract(sin(sn) * c);
}

vec2 poisson_disk[NUM_SAMPLES];
void poisson_dist_sample(const vec2 randomSeed) {
    float ANGLE_STEP = PI2 * float(NUM_RINGS) / float(NUM_SAMPLES);
    float INV_NUM_SAMPLES = 1.0 / float(NUM_SAMPLES);

    float angle = rand_2to1(randomSeed) * PI2;
    float radius = INV_NUM_SAMPLES;
    float radiusStep = radius;

    for(int i = 0; i < NUM_SAMPLES; i++) {
        poisson_disk[i] = vec2(cos(angle), sin(angle)) * pow(radius, 0.75);
        radius += radiusStep;
        angle += ANGLE_STEP;
    }
}

float dynamic_shadow_bias(
    float c,
    float filter_radius_uv,
    vec3 normal,
    vec3 light_pos,
    vec3 frag_pos,
    float frustum_size,
    float shadow_map_size
) {
    normal = normalize(normal);
    vec3 light_dir = normalize(light_pos - frag_pos);
    float frag_size = (1.0 + ceil(filter_radius_uv)) * (frustum_size / shadow_map_size / 2.0);
    return max(frag_size, frag_size * (1.0 - dot(normal, light_dir))) * c;
}

float sample_shadow_map(
    sampler2D shadow_map,
    vec4 light_space_position,
    float bias_c,
    float filter_radius_uv
) {
    float depth = texture(shadow_map, light_space_position.xy).r;
    float cur_depth = light_space_position.z;
    float bias = dynamic_shadow_bias(
        bias_c,
        filter_radius_uv,
        Frag_Normal,
        light_position,
        Frag_Position,
        LIGHT_FRUSTUM_SIZE,
        SHADOW_MAP_SIZE
    );
    if (cur_depth - bias >= depth + EPS) {
        return 0.0;
    } else {
        return 1.0;
    }
}

float pcf_shadow(
    sampler2D shadow_map,
    vec4 light_space_position,
    float bias_c,
    float filter_radius_uv
) {
    poisson_dist_sample(Frag_TexCoords.xy); // xy used as random seed
    float visibility = 0.0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        vec2 offset = poisson_disk[i] * filter_radius_uv;
        visibility += sample_shadow_map(
            shadow_map,
            vec4(light_space_position.xy + offset, light_space_position.z, 1.0),
            bias_c,
            filter_radius_uv
        );
    }
    return visibility / float(NUM_SAMPLES);
}

float find_blocker(
    sampler2D shadow_map,
    vec4 light_space_position,
    float z_receiver
) {
    int num_blockers = 0;
    float sum_blocker_depth = 0.0;
    float z_from_light = light_space_position.z;
    float search_radius = LIGHT_SIZE_UV * (z_from_light - NEAR_PLANE) / z_from_light;
    poisson_dist_sample(Frag_TexCoords.xy); // xy used as random seed
    for (int i = 0; i < NUM_SAMPLES; i++) {
        vec2 offset = poisson_disk[i] * search_radius;
        float depth = texture(shadow_map, light_space_position.xy + offset).r;
        if (depth < z_receiver) {
            sum_blocker_depth += depth;
            num_blockers++;
        }
    }
    if (num_blockers == 0) {
        return -1.0;
    }
    return sum_blocker_depth / float(num_blockers);
}

// Ref: https://developer.download.nvidia.cn/whitepapers/2008/PCSS_Integration.pdf
float pcss_shadow(
    sampler2D shadow_map,
    vec4 light_space_position,
    float bias_c
) {
    float z_receiver = light_space_position.z;
    float avg_blocker_depth = find_blocker(
        shadow_map,
        light_space_position,
        z_receiver
    );
    if (avg_blocker_depth < -EPS) {
        return 1.0;
    }

    float penumbra_size = (z_receiver - avg_blocker_depth) * LIGHT_SIZE_UV / avg_blocker_depth;
    
    return pcf_shadow(
        shadow_map,
        light_space_position,
        bias_c,
        penumbra_size
    );
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

void main() {
    vec3 albedo = pow(texture(albedo_map, Frag_TexCoords.xy).rgb, vec3(2.2));
    float metallic = texture(metallic_map, Frag_TexCoords.xy).r;
    float roughness = texture(roughness_map, Frag_TexCoords.xy).r;

    vec3 N = get_normal_from_map();
    vec3 V = normalize(view_position - Frag_Position);

    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);
    
    vec3 L = normalize(light_position - Frag_Position);
    vec3 H = normalize(V + L);
    float distance = length(light_position - Frag_Position);
    float attenuation = 1.0 / (distance * distance);
    vec3 radiance = light_color * attenuation;
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
    vec3 lighting = (kD * albedo / PI + specular) * radiance * NdotL * 4;

    vec3 ambient = vec3(0.03) * albedo;

    // Perform perspective divide
    vec3 shadow_coord = Frag_LightSpacePosition.xyz / Frag_LightSpacePosition.w;
    // Transform NDC to [0,1] range
    shadow_coord = shadow_coord * 0.5 + 0.5;

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
