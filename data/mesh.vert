#version 450
#pragma shader_stage(vertex)
#include "common.glsli"

layout (location = 0) in vec3 i_position;
layout (location = 1) in vec3 i_normal;
layout (location = 2) in vec2 i_texcoord;

layout (location = 0) out vec3 o_normal;
layout (location = 1) out vec2 o_texcoord;

layout (set = 0, binding = 0) uniform readonly BUFFER(GlobalUniforms, global);
layout (set = 2, binding = 0) uniform readonly BUFFER(StaticMeshUniforms, mesh);

void main() {
	o_normal = i_normal;
	o_texcoord = i_texcoord;
	gl_Position = mesh.proj * mesh.view * mesh.model * vec4(i_position, 1.0);
}
