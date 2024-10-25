#version 450
#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(fragment)
#include "common.glsl"
#include "noise.glsl"

layout (location = 0) out vec4 o_color;

layout (location = 0) in vec3 i_normal;

layout (set = 0, binding = 0) uniform readonly BUFFER(GlobalUniforms, global);
layout (set = 1, binding = 0) uniform readonly BUFFER(MaterialUniforms, material);
layout (set = 2, binding = 0) uniform readonly BUFFER(StaticMeshUniforms, mesh);
layout (set = 1, binding = 1) uniform SAMPLER(_2D, texture_albedo);
layout (set = 1, binding = 2) uniform SAMPLER(_2D, texture_bump);
layout (set = 1, binding = 3) uniform SAMPLER(_2D, texture_specular);

vec3 unit_position_from_latlon(float lat, float lon) {
	lon *= 2.0 * PI;
	lat *= PI;
	return vec3(
		sin(lat) * cos(lon),
		cos(lat),
		sin(lat) * sin(lon)
	);
}

vec2 compute_parallax_mapping(vec2 texcoord, vec3 local_view_dir) {
	float height = texture(texture_bump, texcoord).r;
	vec2 p = local_view_dir.xy / local_view_dir.z * (height * material.smoothness);
	return texcoord - p;
}

float compute_specular_highlight(vec3 normal, float smoothness) {
	float angle = acos(dot(normalize(normalize(global.sun.xyz) - material.view_dir), normal));
	float exponent = angle / smoothness;
	float highlight = exp(-exponent * exponent);
	return highlight;
}

void main() {
	vec3 local_view_dir = normalize(vec3(inverse(mesh.model) * vec4(material.view_dir, 0.0)));
	float lon = atan(i_normal.z, i_normal.x) / (2.0 * PI);
	float lat = acos(i_normal.y) / PI;
	vec2 texcoord = vec2(lon, lat);
	vec3 col = texture(texture_albedo, (texcoord)).rgb;
	vec3 world_normal = normalize(vec3(mesh.model * vec4(i_normal, 0.0)));
	float shadow = clamp(dot(normalize(global.sun.xyz), world_normal), 0.0, 1.0);
	vec3 halfway_dir = normalize(normalize(global.sun.xyz) + material.view_dir);
	const float noise_strength = 0.001;
	float spec = compute_specular_highlight(world_normal
		* mix(1.0-noise_strength, 1.0+noise_strength, snoiseFractal(50.0 * i_normal)), material.smoothness);
	float spec_mask = texture(texture_specular, texcoord).r;
	o_color = vec4((col + spec * spec_mask) * shadow, 1.0);
}
