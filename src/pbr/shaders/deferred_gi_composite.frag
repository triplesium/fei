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

uniform sampler2D direct_lighting;
uniform sampler2D indirect_lighting;

const float normalPower = 64.0f;
const float positionSigma = 24.0f;

vec4 UpsampleIndirectBilateral(vec2 uv)
{
    vec2 lowSize  = vec2(textureSize(indirect_lighting, 0));
    vec2 lowTexel = 1.0 / lowSize;

    vec3 centerPosition = texture(g_position_ao, uv).rgb;
    vec3 centerNormal = normalize(texture(g_normal_roughness, uv).xyz);

    // map full-res uv to low-res continuous texel space
    vec2 lowCoord = uv * lowSize - 0.5;
    vec2 lowBase = floor(lowCoord);
    vec2 fracCoord = fract(lowCoord);

    vec4 sum = vec4(0.0);
    float wsum = 0.0;

    // 2x2 joint bilateral with bilinear base weights to keep edges sharper.
    for (int j = 0; j <= 1; ++j)
    {
        for (int i = 0; i <= 1; ++i)
        {
            vec2 p = (lowBase + vec2(i, j) + 0.5) * lowTexel;
            p = clamp(p, vec2(0.0), vec2(1.0));

            vec4 ind = texture(indirect_lighting, p);

            vec3 samplePosition = texture(g_position_ao, p).rgb;
            vec3 n = normalize(texture(g_normal_roughness, p).xyz);

            float posDelta = length(samplePosition - centerPosition);
            float wDepth  = exp(-posDelta * positionSigma);
            float wNormal = pow(max(dot(centerNormal, n), 0.0), normalPower);

            float wx = (i == 0) ? (1.0 - fracCoord.x) : fracCoord.x;
            float wy = (j == 0) ? (1.0 - fracCoord.y) : fracCoord.y;
            float wSpatial = wx * wy;

            float w = wSpatial * wDepth * wNormal;
            sum += ind * w;
            wsum += w;
        }
    }

    if (wsum > 1e-5) return sum / wsum;
    return texture(indirect_lighting, uv); // fallback
}

void main()
{
    // world-space position
    vec3 position = texture(g_position_ao, Frag_TexCoords).rgb;
    // world-space normal
    vec3 normal = normalize(texture(g_normal_roughness, Frag_TexCoords).xyz);
    // xyz = fragment specular, w = shininess
    vec4 specular = texture(g_specular, Frag_TexCoords);
    // fragment albedo
    vec3 baseColor = texture(g_albedo_metallic, Frag_TexCoords).rgb;
    // convert to linear space
    vec3 albedo = pow(baseColor, vec3(2.2f));
    // fragment emissiviness
    vec3 emissive = texture(g_emissive_depth, Frag_TexCoords).rgb;
    // lighting cumulatives
    vec3 directLighting = texture(direct_lighting, Frag_TexCoords).rgb;
    vec4 indirectLighting = UpsampleIndirectBilateral(Frag_TexCoords);
    vec3 compositeLighting = (directLighting + indirectLighting.rgb) * indirectLighting.a;
    compositeLighting += emissive;

    // Reinhard tone mapping
    compositeLighting = compositeLighting / (compositeLighting + 1.0f);
    // gamma correction
    const float gamma = 2.2;
    // convert to gamma space
    compositeLighting = pow(compositeLighting, vec3(1.0 / gamma));

    fragColor = vec4(compositeLighting, 1.0f);
}
