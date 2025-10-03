#version 450
#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(vertex)
#include "common.glsl"

layout (location = 0) in vec3 i_position;
layout (location = 1) in vec2 i_normal_oct;
layout (location = 2) in float i_tangent_dia;
layout (location = 3) in vec2 i_texcoord;

layout (location = 0) out vec3 o_position;
layout (location = 1) out vec3 o_normal;
layout (location = 2) out vec2 o_texcoord;

// layout (location = 3) out mat3 o_tbn;
layout (location = 3) out vec3 o_tbn_tangent;
layout (location = 4) out vec3 o_tbn_bitangent;
layout (location = 5) out vec3 o_tbn_normal;

// layout (location = 3) out vec3 o_tangent_sun_dir;
// layout (location = 4) out vec3 o_tangent_cam_pos;
// layout (location = 5) out vec3 o_tangent_pos;
// layout (location = 6) out vec3 o_tangent_normal;

layout (set = 0, binding = 0) uniform readonly BUFFER(GlobalUniforms, global);
layout (set = 2, binding = 0) uniform readonly BUFFER(StdMeshUniforms, mesh);

vec2 sign_not_zero(vec2 v) {
	return vec2(
		(v.x >= 0.0) ? +1.0 : -1.0,
		(v.y >= 0.0) ? +1.0 : -1.0
	);
}

vec3 oct_to_float32x3(vec2 e) {
	vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
	if (v.z < 0) v.xy = (1.0 - abs(v.yx)) * sign_not_zero(v.xy);
	return normalize(v);
}

vec2 decode_diamond(float p) {
	vec2 v;

	// Remap p to the appropriate segment on the diamond
	float p_sign = sign(p - 0.5f);
	v.x = -p_sign * 4.f * p + 1.f + p_sign * 2.f;
	v.y = p_sign * (1.f - abs(v.x));

	// Normalization extends the point on the diamond back to the unit circle
	return normalize(v);
}

vec3 decode_tangent(vec3 normal, float diamond_tangent) {
	// As in the encode step, find our canonical tangent basis span(t1, t2)
	vec3 t1;
	if (abs(normal.y) > abs(normal.z)) {
		t1 = vec3(normal.y, -normal.x, 0.f);
	} else {
		t1 = vec3(normal.z, 0.f, -normal.x);
	}
	t1 = normalize(t1);

	vec3 t2 = cross(t1, normal);

	// Recover the coordinates used with t1 and t2
	vec2 packed_tangent = decode_diamond(diamond_tangent);

	return t1 * packed_tangent.x + t2 * packed_tangent.y;
}

void main() {
	o_normal = oct_to_float32x3(i_normal_oct);
	vec3 tangent = decode_tangent(o_normal, i_tangent_dia);
	o_texcoord = i_texcoord;

	// mat3 unscaled_model = transpose(inverse(mat3(mesh.unscaled_model)));

	o_position = i_position; // unscaled_model * 

	vec3 T = normalize((mesh.unscaled_model * vec4(tangent, 0.0)).xyz);
	vec3 N = normalize((mesh.unscaled_model * vec4(o_normal, 0.0)).xyz);
	T = normalize(T - dot(T, N) * N);
	vec3 B = cross(N, T);
	o_tbn_tangent = T;
	o_tbn_bitangent = B;
	o_tbn_normal = N;
	// mat3 TBN = mat3(T, B, N);
	// o_tbn = TBN;

	// TBN = transpose(TBN);

	// vec3 local_model_pos = unscaled_model * i_position;
	// o_tangent_sun_dir = TBN * -mesh.sun.xyz;
	// o_tangent_cam_pos = TBN * mesh.rel_cam_pos.xyz;
	// o_tangent_pos = TBN * local_model_pos;
	// o_tangent_normal = TBN * o_normal;

	gl_Position = mesh.proj * mesh.model_view * vec4(i_position, 1.0);
}
