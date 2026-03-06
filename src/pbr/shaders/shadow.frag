#version 450 core

layout(location = 0) out vec4 outColor;

in vec2 Frag_TexCoords;

// uniform vec2 exponents;
// uniform float alphaCutoff = 0.01f;

const vec2 exponents = vec2(40.0f, 5.0f);

vec2 WarpDepth(float depth)
{
    // rescale depth into [-1, 1]
    depth = 2.0 * depth - 1.0;
    float pos =  exp( exponents.x * depth);
    float neg = -exp(-exponents.y * depth);
	
    return vec2(pos, neg);
}

vec4 ShadowDepthToEVSM(float depth)
{
	vec2 moment1 = WarpDepth(depth);
	vec2 moment2 = moment1 * moment1;

	return vec4(moment1, moment2);
}

void main()
{
	outColor = ShadowDepthToEVSM(gl_FragCoord.z);
}
