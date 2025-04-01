#version 450
#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(fragment)
#include "common.glsl"

layout (location = 0) out vec4 o_color;

void main() {
	o_color = vec4(vec3(100.0), 1.0);
}
