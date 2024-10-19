#version 450
#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(fragment)
#include "common.glsli"

layout (location = 0) out vec4 o_color;

layout (location = 0) in vec3 i_normal;

layout (set = 0, binding = 0) uniform readonly BUFFER(GlobalUniforms, global);
layout (set = 1, binding = 0) uniform readonly BUFFER(MaterialUniforms, material);
layout (set = 2, binding = 0) uniform readonly BUFFER(StaticMeshUniforms, mesh);
layout (set = 1, binding = 1) uniform sampler2D texture_albedo;
layout (set = 1, binding = 2) uniform sampler2D texture_bump;
layout (set = 1, binding = 3) uniform sampler2D texture_specular;

vec3 unit_position_from_latlon(float lat, float lon) {
	lon *= 2.0 * PI;
	lat *= PI;
	return vec3(
		sin(lat) * cos(lon),
		cos(lat),
		sin(lat) * sin(lon)
	);
}

void main() {
	float lon = atan(i_normal.z, i_normal.x) / (2.0 * PI);
	float lat = acos(i_normal.y) / PI;
	vec2 texcoord = vec2(lon, lat);
	vec3 col = texture(texture_albedo, texcoord).rgb;

	vec3 normal  = normalize(unit_position_from_latlon(lat, lon));
	vec3 tangent = vec3(-sin(lon * 2.0 * PI), 0.0, cos(lon * 2.0 * PI));

	vec3 N = normalize(vec3(mesh.model * vec4( normal, 0.0)));
	vec3 T = normalize(vec3(mesh.model * vec4(tangent, 0.0)));
	T = normalize(T - dot(T, N) * N);
	vec3 B = cross(N, T);
	mat3 TBN = (mat3(T, B, N));
	vec3 world_normal = normalize(TBN * normal);

	float shadow = dot(normalize(global.sun.xyz), N);
	shadow = clamp(shadow, 0.0, 1.0);
	o_color = vec4(col * mix(0.0, 1.0, shadow), 1.0);
}
