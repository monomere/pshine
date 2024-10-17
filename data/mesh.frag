#version 450
#pragma shader_stage(fragment)
#include "common.glsli"

layout (location = 0) out vec4 o_color;

layout (location = 0) in vec3 i_normal;
layout (location = 1) in vec2 i_texcoord;

layout (set = 0, binding = 0) uniform readonly BUFFER(GlobalUniforms, global);
layout (set = 1, binding = 0) uniform readonly BUFFER(MaterialUniforms, material);
layout (set = 1, binding = 1) uniform sampler2D texture_base;

void main() {
	float f = dot(normalize(global.sun.xyz), normalize(i_normal));
	vec3 dir = i_normal;
	float lon = 0.75 + atan(dir.z, dir.x) / (2.0 * PI);
	float lat = 0.5 - asin(dir.y) / PI;
	vec2 texcoords = vec2(lon, lat);
	vec3 col = texture(texture_base, texcoords).rgb;
	// vec3 col = vec3(texcoords, 1.0).rgb;
	o_color = vec4(col * f, 1.0); // material.color.rgb
}
