#version 450 core

out gl_PerVertex {
	vec4 gl_Position;
};

layout (location = 0) in vec3 in_pos;
layout (location = 1) in vec3 in_nor;
layout (location = 2) in vec2 in_tex;

layout (std140, binding = 0) uniform ubo {
	mat4 mvp;
} ubos;

layout (location = 0) out v_out_block {
	vec3 nor;
} v_out;

void main() {
	v_out.nor = in_nor;
	gl_Position = ubos.mvp * vec4(in_pos, 1.0);
}
