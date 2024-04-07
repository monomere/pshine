#version 450

layout (location = 0) out vec4 out_col;

layout (location = 0) in vec2 i_uv;

layout (set = 0, binding = 0) uniform GLOBAL_UNIFORMS {
	mat4 view;
	mat4 proj;
	vec4 sun;
	vec4 camera; // xyz, nearz
} global;

layout (set = 2, binding = 0) uniform ATMO_UNIFORMS {
	vec4 planet; // xyz, w=radius
	float radius;
	float density_falloff;
	uint optical_depth_samples;
	uint scatter_point_samples;
	float blend_factor;
	vec3 wavelengths;
} atmo;

const float MAX_FLOAT = 3.402823466e+38;

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

float compute_density(vec3 pos) {
	float height = length(pos - atmo.planet.xyz) - atmo.planet.w;
	float height01 = height / (atmo.radius - atmo.planet.w);
	return exp(-height01 * atmo.density_falloff) * (1 - height01);
}

float optical_depth(vec3 ray_origin, vec3 ray_dir, float ray_length) {
	vec3 current = ray_origin;
	float step_size = ray_length / (atmo.optical_depth_samples);
	float total = 0.0;
	for (uint i = 0; i < atmo.optical_depth_samples; ++i) {
		float local_density = compute_density(current);
		total += local_density * step_size;
		current += ray_dir * step_size;
	}
	return total;
}

float compute_light(vec3 ray_origin, vec3 ray_dir, float ray_length) {
	vec3 current = ray_origin;
	float step_size = ray_length / (atmo.scatter_point_samples);
	float total = 0.0;

	for (uint i = 0; i < atmo.scatter_point_samples; ++i) {
		float sun_ray_length = intersect_ray_sphere(
			atmo.planet.xyz,
			atmo.radius,
			current,
			normalize(global.sun.xyz)
		).y;
		float sun_ray_optical_depth = optical_depth(
			current,
			normalize(global.sun.xyz),
			sun_ray_length
		);
		float view_ray_optical_depth = optical_depth(
			current,
			-ray_dir,
			step_size * i
		);
		float transmittance = exp(-(sun_ray_optical_depth + view_ray_optical_depth));
		float local_density = compute_density(current);
		total += local_density * transmittance * step_size;
		current += ray_dir * step_size;
	}
	return total;
}

vec4 compute_color(vec4 col, float depth) {
	vec3 ray_origin = global.camera.xyz;
	vec2 ray_ndc = vec2(i_uv.x, i_uv.y) * 2.0 - 1.0;
	vec4 ray_clip = vec4(ray_ndc, 0.0, 1.0);
	vec4 ray_eye = inverse(global.proj) * ray_clip;
	vec4 ray_eye2 = vec4(ray_eye.xy, 1.0, 0.0);
	vec3 ray_dir = normalize((inverse(global.view) * ray_eye).xyz);
	// return vec4(ray_dir.xyz, 1.0);

	vec2 surface_hit = intersect_ray_sphere(
		atmo.planet.xyz,
		atmo.planet.w,
		ray_origin,
		ray_dir
	);

	float dst_to_surface = surface_hit.x;

	vec2 atmo_hit = intersect_ray_sphere(
		atmo.planet.xyz,
		atmo.radius,
		ray_origin,
		ray_dir
	);

	float dst_to_atmo = atmo_hit.x;

	if (dst_to_atmo < depth) return col;

	// return clamp(vec4(vec3(0.0), 1.0) + surface_hit.y, 0, 1);

	float dst_thru_atmo = min(
		atmo_hit.y,
		dst_to_surface - dst_to_atmo
	);

	if (dst_thru_atmo > 0.0) {
		// return vec4(vec3(
		// 	dst_thru_atmo / (2.0 * atmo.radius)
		// ), 1.0);
		const float epsilon = 0.0001;
		vec3 ray_origin_atmo = ray_origin + ray_dir * (dst_to_atmo + epsilon);
		float light = compute_light(
			ray_origin_atmo,
			ray_dir,
			dst_thru_atmo - epsilon * 2.0
		);
		
		return col * (1 - light) + light; // vec4(ray_origin_atmo, 1.0)
	}

	return col;
}

float linearize_depth(float depth) {
	return global.camera.w / (depth * 2 - 1);
	// float n = 1000.0;
	// float f = global.camera.w;
	// return n * f / (f + depth * (n - f));
	// return (2.0 * 100.0)
	// 	/ (1000.0 + global.camera.w + depth * (1000.0 - global.camera.w)) ;
	// return global.camera.w / depth;
}

layout (input_attachment_index = 0, set = 2, binding = 1) uniform subpassInput u_input_color;
layout (input_attachment_index = 1, set = 2, binding = 2) uniform subpassInput u_input_depth;

void main() {
	vec4 color = subpassLoad(u_input_color).rgba;
	float depth = subpassLoad(u_input_depth).r;
	float linear_depth = linearize_depth(depth);
	// out_col = mix(color, compute_color(color, linear_depth), atmo.blend_factor);
	// vec4(vec3(depth), 1.0)
	out_col = mix(compute_color(color, linear_depth), color, atmo.blend_factor);
}
