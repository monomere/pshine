#version 450
#pragma shader_stage(fragment)
#include "common.glsli"

layout (location = 0) out vec4 o_color;

layout (location = 0) in vec3 i_normal;
layout (location = 1) in vec2 i_texcoord;

layout (set = 0, binding = 0) uniform readonly BUFFER(GlobalUniforms, global);
layout (set = 1, binding = 0) uniform readonly BUFFER(MaterialUniforms, material);

void main() {
	float f = dot(normalize(global.sun.xyz), normalize(i_normal));
	o_color = vec4(material.color.rgb * f, 1.0);
}
