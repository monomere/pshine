#version 450
#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(vertex)
#include "common.glsl"

layout (location = 0) in vec3 i_position;
layout (location = 1) in vec2 i_normal_oct;
layout (location = 2) in float i_tangent_dia;

layout (location = 0) out vec3 o_normal;

layout (set = 0, binding = 0) uniform readonly BUFFER(GlobalUniforms, global);
layout (set = 2, binding = 0) uniform readonly BUFFER(StaticMeshUniforms, mesh);

vec2 sign_not_zero(vec2 v) {
	return vec2((v.x >= 0.0) ? +1.0 : -1.0, (v.y >= 0.0) ? +1.0 : -1.0);
}

vec3 oct_to_float32x3(vec2 e) {
	vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
	if (v.z < 0) v.xy = (1.0 - abs(v.yx)) * sign_not_zero(v.xy);
	return normalize(v);
}

void main() {
	o_normal = oct_to_float32x3(i_normal_oct);
	gl_Position = mesh.proj * mesh.model_view * vec4(i_position, 1.0);
}
