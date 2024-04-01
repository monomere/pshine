#version 450

layout (location = 0) in vec3 i_position;
layout (location = 1) in vec3 i_normal;
layout (location = 2) in vec2 i_texcoord;

layout (location = 0) out vec3 o_normal;

layout (set = 0, binding = 0) uniform GLOBAL_UNIFORMS {
	vec4 unused;
} global;

layout (set = 2, binding = 0) uniform OBJECT_UNIFORMS {
	mat4 mvp;
} object;

void main() {
	o_normal = i_normal;
	gl_Position = object.mvp * vec4(i_position, 1.0);
}
