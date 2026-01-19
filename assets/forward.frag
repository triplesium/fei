#version 450 core
in vec3 Frag_Position;
in vec3 Frag_Normal;
in vec2 Frag_TexCoords;
in vec4 Frag_LightSpacePosition;
out vec4 Out_Color;

layout(binding = 0, row_major, std140) uniform Material {
    vec3 base_color;
    int material_flags;
};
layout(binding = 1, row_major, std140) uniform View {
    mat4 view_projection;
    vec3 view_position;
};
layout(binding = 2, row_major, std140) uniform Mesh {
    mat4 model;
};
layout(binding = 3, row_major, std140) uniform Light {
    mat4 light_view_projection;
    vec3 light_position;
    vec3 light_color;
};
layout(binding = 1) uniform sampler2D diffuse_texture;
layout(binding = 2) uniform sampler2D shadow_map;

const int MATERIAL_FLAG_HAS_BASE_COLOR_TEXTURE = 1;

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

vec3 blinn_phong(
    vec3 frag_pos,
    vec3 normal,
    vec3 view_pos,
    vec3 light_pos,
    vec3 light_color
) {
    // Ambient
    vec3 ambient = 0.05 * light_color;
    // Diffuse
    vec3 light_dir = normalize(light_pos - frag_pos);
    float diff = max(dot(normal, light_dir), 0.0);
    vec3 diffuse = diff * light_color;
    // Specular
    vec3 view_dir = normalize(view_pos - frag_pos);
    vec3 halfway_dir = normalize(light_dir + view_dir);
    float spec = pow(max(dot(normal, halfway_dir), 0.0), 32.0);
    vec3 specular = spec * light_color;
    return (ambient + diffuse + specular);
}

void main() {
    vec3 color = base_color;
    if ((material_flags & MATERIAL_FLAG_HAS_BASE_COLOR_TEXTURE) != 0) {
        color *= texture(diffuse_texture, Frag_TexCoords.xy).rgb;
    }
    
    // Perform perspective divide
    vec3 shadow_coord = Frag_LightSpacePosition.xyz / Frag_LightSpacePosition.w;
    // Transform NDC to [0,1] range
    shadow_coord = shadow_coord * 0.5 + 0.5;

    vec3 lighting = blinn_phong(Frag_Position, normalize(Frag_Normal), view_position, light_position, light_color);
    // float visibility = sample_shadow_map(
    //     shadow_map,
    //     vec4(shadow_coord, 1.0),
    //     0.4,
    //     0.0
    // );
    // float visibility = pcf_shadow(
    //     shadow_map,
    //     vec4(shadow_coord, 1.0),
    //     0.08,
    //     SAMPLE_RADIUS / SHADOW_MAP_SIZE
    // );
    float visibility = pcss_shadow(
        shadow_map,
        vec4(shadow_coord, 1.0),
        0.08
    );
    Out_Color = vec4(color * lighting * visibility, 1.0);
}
