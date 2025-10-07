#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(fragment)
#include "common.glsl"
#include "noise.glsl"

layout (location = 0) out vec4 o_color0; // unused
layout (location = 1) out vec4 o_diffuse_o;
layout (location = 2) out vec4 o_normal_r_m;
layout (location = 3) out vec4 o_emissive;
// layout (location = 4) out uint o_object;

layout (location = 0) in vec3 i_position;
layout (location = 1) in vec3 i_normal;
layout (location = 2) in vec2 i_texcoord;

// layout (location = 3) in mat3 i_tbn;
layout (location = 3) in vec3 i_tbn_tangent;
layout (location = 4) in vec3 i_tbn_bitangent;
layout (location = 5) in vec3 i_tbn_normal;
// layout (location = 3) in vec3 i_tangent_sun_dir;
// layout (location = 4) in vec3 i_tangent_cam_pos;
// layout (location = 5) in vec3 i_tangent_pos;
// layout (location = 6) in vec3 i_tangent_normal;

layout (set = 0, binding = 0) uniform readonly BUFFER(GlobalUniforms, global);
layout (set = 1, binding = 0) uniform readonly BUFFER(StdMaterialUniforms, material);
layout (set = 2, binding = 0) uniform readonly BUFFER(StdMeshUniforms, mesh);
layout (set = 1, binding = 1) uniform SAMPLER(_2D, texture_diffuse);
layout (set = 1, binding = 2) uniform SAMPLER(_2D, texture_ao_rough_metal);
layout (set = 1, binding = 3) uniform SAMPLER(_2D, texture_normal);
layout (set = 1, binding = 4) uniform SAMPLER(_2D, texture_emissive);

// vec4 _vis(vec3 x) {
// 	return vec4(clamp(abs(x), 0.0, 1.0), 1.0);
// }

vec2 sign_not_zero(vec2 v) {
	return vec2(
		(v.x >= 0.0) ? +1.0 : -1.0,
		(v.y >= 0.0) ? +1.0 : -1.0
	);
}

vec2 float32x3_to_oct(in vec3 v) {
	// Project the sphere onto the octahedron, and then onto the xy plane
	vec2 p = v.xy * (1.0 / (abs(v.x) + abs(v.y) + abs(v.z)));
	// Reflect the folds of the lower hemisphere over the diagonals
	return (v.z <= 0.0) ? ((1.0 - abs(p.yx)) * sign_not_zero(p)) : p;
}

void main() {
	vec3 diffuse = texture(texture_diffuse, i_texcoord).rgb;
	vec3 ao_rough_metal = texture(texture_ao_rough_metal, i_texcoord).rgb;
	vec3 emissive = texture(texture_emissive, i_texcoord).rgb;
	vec3 normal_map = normalize(texture(texture_normal, i_texcoord).rgb * 2.0 - 1.0);

	float occlusion = ao_rough_metal.r;
	float roughness = ao_rough_metal.g;
	float metallic = ao_rough_metal.b;

	mat3 tbn = mat3(i_tbn_tangent, i_tbn_bitangent, i_tbn_normal);
	// mat3 tbn = i_tbn;
	o_diffuse_o = vec4(diffuse, occlusion);
	o_normal_r_m = vec4(float32x3_to_oct(normalize(tbn * normal_map)), roughness, metallic); //  * normal_map
	o_emissive = vec4(emissive, 0.0);
}
