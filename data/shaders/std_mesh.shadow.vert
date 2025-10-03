#version 450
#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(vertex)
#include "common.glsl"

layout (location = 0) in vec3 i_position;
layout (location = 1) in vec2 i_normal_oct;
layout (location = 2) in float i_tangent_dia;
layout (location = 3) in vec2 i_texcoord;

layout (set = 0, binding = 0) uniform readonly BUFFER(StdMeshUniforms, mesh);

void main() {
	gl_Position = mesh.proj * mesh.model_view * vec4(i_position, 1.0);
}
