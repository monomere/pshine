#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(fragment)
#include "common.glsl"

layout (location = 0) out vec4 o_color;
layout (location = 0) in vec4 i_clip_pos;

void main() {
	o_color = vec4(vec3(i_clip_pos.z), 1.0);
}
