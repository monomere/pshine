#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(vertex)
#include "common.glsl"

// The shader is compiled to mesh.1.vert and mesh.2.vert
// with SET_INDEX set to 1 and 2 accordingly.
// I would've use specialization constants, but you can't it seems :(
#ifndef SET_INDEX
#define SET_INDEX 2
#endif

layout (location = 0) in vec3 i_position;
layout (location = 1) in vec2 i_normal_oct;
layout (location = 2) in float i_tangent_dia;

layout (location = 0) out vec3 o_normal;

layout (set = 0, binding = 0) uniform readonly BUFFER(GlobalUniforms, global);
layout (set = SET_INDEX, binding = 0) uniform readonly BUFFER(StaticMeshUniforms, mesh);

vec2 sign_not_zero(vec2 v) {
	return vec2((v.x >= 0.0) ? +1.0 : -1.0, (v.y >= 0.0) ? +1.0 : -1.0);
}

vec3 oct_to_float32x3(vec2 e) {
	vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
	if (v.z < 0) v.xy = (1.0 - abs(v.yx)) * sign_not_zero(v.xy);
	return normalize(v);
}

void calculate_lat_lon(out float lat, out float lon, out vec3 normal) {
	vec3 p = normalize(i_position);
	bool xz_zero = ((abs(p.x) <= EPSILON) && (abs(p.z) <= EPSILON));
	bool y_zero = abs(p.y) <= EPSILON;
	if (xz_zero) {
		lat = 0.0;
		lon = 0.0;
		if (y_zero) {
			normal = vec3(1.0, 0.0, 0.0);
		} else {
			normal = p;
		}
	} else {
		lon = atan(p.z, p.x) / (2.0 * PI);
		lat = acos(clamp(p.y, -1.0, 1.0)) / PI;
		normal = p;
	}
}

float calculate_height_at(float lat, float lon) {
	const float h = 0.1;
	const float f = 10.0 * PI;
	return h * sin(f * lat) * cos(f * lon);
}

void main() {
	o_normal = oct_to_float32x3(i_normal_oct);
	vec3 position = i_position;
	vec3 dir = normalize(position);
	float lat, lon;
	vec3 norm;
	calculate_lat_lon(lat, lon, norm);

	// TODO: recalculate normals

	// position += dir * calculate_height_at(lat, lon);
	gl_Position = mesh.proj * mesh.model_view * vec4(position, 1.0);
}
