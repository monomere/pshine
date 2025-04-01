#version 450
#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(fragment)
#include "common.glsl"

layout (location = 0) out vec4 o_col;
layout (location = 0) in vec3 i_pos;

layout (set = 0, binding = 0) uniform SAMPLER(_CUBE, cubemap);

void main() {
	vec3 col = texture(cubemap, i_pos).rgb;
	float lum = (col.r + col.g + col.b) / 3; // TODO: perceived luminance
	float mapped_lum = 0.1 + 20.0 * pow(lum, 6.0);
	o_col = vec4(col * mapped_lum, 1.0);
}
