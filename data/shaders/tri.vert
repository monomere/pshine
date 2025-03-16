#version 450

vec2[3] positions = vec2[](
	vec2(0.0, -0.5),
	vec2(0.5, 0.5),
	vec2(-0.5, 0.5)
);

vec3[3] colors = vec3[](
	vec3(1.0, 0.0, 0.0),
	vec3(0.0, 1.0, 0.0),
	vec3(0.0, 0.0, 1.0)
);

layout (location = 0) out vec3 o_color;

void main() {
	gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
	o_color = colors[gl_VertexIndex];
}
