#ifndef ATMO_COMMON_GLSLI_
#define ATMO_COMMON_GLSLI_
#include "common.glsl"

vec2 intersect_ray_sphere(
	vec3 sphere_center,
	float sphere_radius,
	vec3 ray_origin,
	vec3 ray_dir
) {
	vec3 local_ray_origin = ray_origin - sphere_center;
	float b = dot(local_ray_origin, normalize(ray_dir));
	float c
		= dot(local_ray_origin, local_ray_origin)
		- sphere_radius * sphere_radius;
	float d = b * b - c;

	if (d >= 0.0) {
		float s = sqrt(d);
		float near = max(0.0, (-b - s));
		float far = (-b + s);
		if (far >= 0.0) return vec2(near, far - near);
	}

	return vec2(MAX_FLOAT, 0.0);
}

float compute_density(
	vec3 rel_pos,
	float falloff,
	float planet_radius,
	float atmo_height
) {
	float h = max(length(rel_pos) - planet_radius, 0.0) / atmo_height;
	return exp(-h * falloff) * (1.0 - h);
}

#endif // ATMO_COMMON_GLSLI_
