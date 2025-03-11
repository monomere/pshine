#version 450
#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(fragment)
#include "common.glsl"

layout (location = 0) out vec4 o_col;

layout (location = 0) in vec2 i_uv;

layout (set = 0, binding = 0) uniform readonly BUFFER(GlobalUniforms, global);
layout (set = 1, binding = 0) uniform readonly BUFFER(RingsUniforms, rings);
layout (set = 1, binding = 1) uniform SAMPLER(_2D, rings_slice);

bool ordered(float a, float b, float c) { return a <= b && b <= c; }

void main() {
	vec2 uv = i_uv * 2.0 - 1.0;
	float d = length(uv);
	float a = atan(uv.y, uv.x);
	float a01 = a / (2.0 * PI);
	vec3 sun_dir = rings.sun.xyz;
	if (d > 1.0) discard;
	float f = rings.inner_radius / rings.outer_radius;
	if (d < f) discard;
	vec4 c = texture(rings_slice, vec2((d - f) / (1.0 - f), a01 * 16.0)).rgba;
	if (c.a < 0.1) discard;
	float sa = atan(-rings.sun.z, -rings.sun.x);
	float dsa = abs(asin(rings.rel_planet_radius / d));
	if (
		ordered(sa - dsa, a, sa + dsa) ||
		ordered(sa - dsa, a - 2 * PI, sa + dsa) ||
		ordered(sa - dsa, a + 2 * PI, sa + dsa)
	) {
		o_col = vec4(vec3(0.0), 1.0);
	} else {
		o_col = c;
	}
	// o_col = vec4(vec3(rings.rel_planet_radius), 1.0);
}


