#version 460
#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(fragment)
#include "common.glsl"

layout (location = 0) out vec4 o_col;
layout (location = 0) in vec2 i_uv;
layout (input_attachment_index = 0, set = 0, binding = 0) uniform SUBPASS_INPUT(u_input_color);

// https://64.github.io/tonemapping/#uncharted-2
vec3 uncharted2_tonemap_partial(vec3 x) {
	float A = 0.15;
	float B = 0.50;
	float C = 0.10;
	float D = 0.20;
	float E = 0.02;
	float F = 0.30;
	return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

vec3 uncharted2_filmic(vec3 v) {
	float exposure_bias = 2.0;
	vec3 curr = uncharted2_tonemap_partial(v * exposure_bias);
	vec3 W = vec3(11.2);
	vec3 white_scale = vec3(1.0) / uncharted2_tonemap_partial(W);
	return curr * white_scale;
}

void main() {
	vec4 col = subpassLoad(u_input_color).rgba;
	o_col = vec4(uncharted2_filmic(col.rgb), col.a);
}
