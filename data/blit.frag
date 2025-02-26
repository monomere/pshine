#version 460
#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(fragment)
#include "common.glsl"

layout (location = 0) out vec4 o_col;
layout (location = 0) in vec2 i_uv;
layout (input_attachment_index = 0, set = 0, binding = 0) uniform SUBPASS_INPUT(u_input_color);

void main() {
	o_col = subpassLoad(u_input_color).rgba;
}
