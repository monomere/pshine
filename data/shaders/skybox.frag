#version 450
#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(fragment)
#include "common.glsl"

layout (location = 0) out vec4 o_col;
layout (location = 0) in vec3 i_pos;

layout (set = 0, binding = 0) uniform SAMPLER(_CUBE, cubemap);

void main() {
	o_col = vec4(texture(cubemap, i_pos).rgb, 1.0) * 0.2;
}
