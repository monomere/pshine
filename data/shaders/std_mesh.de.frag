#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(fragment)
#include "common.glsl"
#include "noise.glsl"

layout (location = 0) out vec4 o_color0; // unused
layout (location = 1) out vec4 o_diffuse_o;
layout (location = 2) out vec4 o_normal_r_m;
layout (location = 3) out vec4 o_emissive_s;
// layout (location = 4) out uint o_object;

layout (location = 0) in vec3 i_position;
layout (location = 1) in vec3 i_normal;
layout (location = 2) in vec2 i_texcoord;

// layout (location = 3) in mat3 i_tbn;
layout (location = 3) in vec3 i_tbn_tangent;
layout (location = 4) in vec3 i_tbn_bitangent;
layout (location = 5) in vec3 i_tbn_normal;
layout (location = 6) in vec4 i_shadow_fragcoord;
layout (location = 7) in vec4 i_fragcoord;
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
layout (set = 1, binding = 5) uniform SAMPLER(_2D, shadow_map);

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

float linearize_depth(float depth) {
	return global.camera.w / depth;
}

float sample_3x3_tent(sampler2D i_tex, vec2 uv) {
	vec2 o = vec2(1.0, 1.0) / vec2(textureSize(i_tex, 0));
	float a = texture(i_tex, uv + vec2(-o.x,  o.y)).r;
	float b = texture(i_tex, uv + vec2(   0,  o.y)).r;
	float c = texture(i_tex, uv + vec2( o.x,  o.y)).r;
	float d = texture(i_tex, uv + vec2(-o.x,    0)).r;
	float e = texture(i_tex, uv + vec2(   0,    0)).r;
	float f = texture(i_tex, uv + vec2( o.x,    0)).r;
	float g = texture(i_tex, uv + vec2(-o.x, -o.y)).r;
	float h = texture(i_tex, uv + vec2(   0, -o.y)).r;
	float i = texture(i_tex, uv + vec2( o.x, -o.y)).r;

	return ((a + c + g + i) + (b + d + f + h) * 2.0 + e * 4.0) / 16.0;
	return e;
}

// float fetch_shadow_map(vec2 pos) {
// 	vec2 pix = 1.0 / vec2(textureSize(shadow_map, 0));
// 	float res = 0.0;
	
// 	return res;
// }

float compute_shadow(out vec3 extra, in vec3 normal) {
	vec3 shadow_coord = i_shadow_fragcoord.xyz / i_shadow_fragcoord.w;
	float shadow_depth = sample_3x3_tent(shadow_map, shadow_coord.xy * 0.5 + 0.5);
	shadow_depth = 1.0 / (1 - shadow_depth) - 1.0;
	float current_depth = i_shadow_fragcoord.z / i_shadow_fragcoord.w;
	extra = shadow_coord;
	float bias = clamp(0.01 * (-dot(global.sun.xyz, normal)), 0.0, 0.01);
	return float(current_depth - bias > shadow_depth);
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
	vec3 normal = normalize(tbn * normal_map);

	vec3 extra = vec3(0.0);
	float shadow = compute_shadow(extra, normal);

	// mat3 tbn = i_tbn;
	o_diffuse_o = vec4(diffuse, occlusion);
	o_normal_r_m = vec4(float32x3_to_oct(normal), roughness, metallic); //  * normal_map
	o_emissive_s = vec4(emissive.rgb, shadow);
	// o_emissive_s = vec4(extra, shadow);
}
