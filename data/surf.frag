#version 450 core

layout (location = 0) out vec4 out_col;

layout (location = 0) in v_in_block {
	vec3 nor;
} v_in;

void main() {
	vec3 sun_dir = normalize(vec3(2.0, 3.0, -1.0));
	float f = mix(0.2, 1.0, dot(sun_dir, v_in.nor));
	out_col = vec4(0.3, 0.2, 0.4, 1.0) * f;
}
