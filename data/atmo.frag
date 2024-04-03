#version 450

layout (location = 0) out vec4 out_col;

layout (location = 0) in vec2 i_uv;

layout (set = 0, binding = 0) uniform GLOBAL_UNIFORMS {
	vec4 sun;
	vec4 camera; // plane, znear, unused
	vec4 camera_up;
	vec4 camera_right;
	vec4 camera_pos;
} global;

layout (set = 2, binding = 0) uniform ATMO_UNIFORMS {
	vec4 planet; // xyz, w=radius
	float radius;
	float density_falloff;
	uint optical_depth_samples;
	uint scatter_point_samples;
} atmo;

vec2 intersect_ray_sphere(
	vec3 sphere_center,
	float sphere_radius,
	vec3 ray_origin,
	vec3 ray_direction
) {
	vec3 local_ray_origin = ray_origin - sphere_center;
	float b = 2 * dot(local_ray_origin, ray_direction);
	float c
		= dot(local_ray_origin, local_ray_origin)
		- sphere_radius * sphere_radius;
	float d = b * b - 4.0 * c;

	if (d > 0.0) {
		float s = sqrt(d);
		float near = max(0.0, (-b - s) / 2.0);
		float far = (-b + s) / 2.0;
		if (far >= 0.0) return vec2(near, far - near);
	}

	return vec2(1.0 / 0.0, 0.0);
}

float compute_local_density(vec3 pos) {
	float height = length(atmo.planet.xyz - pos) - atmo.planet.w;
	float height01 = height / (atmo.radius - atmo.planet.w);
	return exp(-height01 * atmo.density_falloff) * (1 - height01);
}

float compute_optical_depth(
	vec3 ray_origin,
	vec3 ray_direction,
	float ray_length
) {
	vec3 current = ray_origin;
	float total = 0.0;
	float ray_step_length = ray_length / (atmo.optical_depth_samples - 1);
	for (int i = 0; i < atmo.optical_depth_samples; ++i) {
		float local_density = compute_local_density(current);
		total += local_density * ray_step_length;
		current += ray_direction * ray_step_length;
	}
	return total;
}

float compute_light(
	vec3 ray_origin,
	vec3 ray_direction,
	float ray_length
) {
	vec3 current = ray_origin;
	float ray_step_length = ray_length / (atmo.scatter_point_samples - 1);
	float total = 0.0;
	for (int i = 0; i < atmo.scatter_point_samples; ++i) {
		float sun_ray_len = intersect_ray_sphere(
			atmo.planet.xyz,
			atmo.radius,
			current,
			normalize(-global.sun.xyz)
		).y;
		float sun_ray_optical_depth = compute_optical_depth(
			current,
			normalize(-global.sun.xyz),
			sun_ray_len
		);
		float view_ray_optical_depth = compute_optical_depth(
			current,
			-ray_direction,
			ray_step_length * i
		);
		float transmittance = exp(-(sun_ray_optical_depth + view_ray_optical_depth));
		float local_density = compute_local_density(current);
		total += local_density * transmittance * ray_step_length;
		current += ray_direction * ray_step_length;
	}
	return total;
}

vec4 compute_color(vec4 col, float depth) {
	vec2 uv_flipped = vec2(i_uv.x, 1.0 - i_uv.y);
	vec2 ray_uv = global.camera.xy * (uv_flipped * 2.0 - 1.0);

	vec3 ray_target
		= cross(global.camera_right.xyz, global.camera_up.xyz)
			* global.camera.z
		+ global.camera_right.xyz * ray_uv.x
		+ global.camera_up.xyz * ray_uv.y;

	vec3 ray_origin = global.camera_pos.xyz;
	vec3 ray_direction = normalize(ray_target);

	float dst_to_surface = intersect_ray_sphere(
		atmo.planet.xyz,
		atmo.planet.w,
		ray_origin,
		ray_direction
	).x;

	vec2 hit = intersect_ray_sphere(
		atmo.planet.xyz,
		atmo.radius,
		ray_origin,
		ray_direction
	);

	float dst_to_atmo = hit.x;
	float dst_thru_atmo = min(
		hit.y,
		dst_to_surface - dst_to_atmo
	);

	if (dst_thru_atmo > 0.0) {
		// return vec4(vec3(
		// 	dst_thru_atmo / (2.0 * atmo.radius)
		// ), 1.0);
		const float epsilon = 0.0001;
		vec3 ray_origin_atmo = ray_origin + ray_direction * (dst_to_atmo + epsilon);
		float light = compute_light(ray_origin_atmo, ray_direction, dst_thru_atmo - epsilon * 2.0);
		return col * (1 - light) + light;
	}

	return col;
}

float linearize_depth(float depth) {
	return global.camera.z / depth;
}

layout (input_attachment_index = 0, set = 2, binding = 1) uniform subpassInput u_input_color;
layout (input_attachment_index = 1, set = 2, binding = 2) uniform subpassInput u_input_depth;

void main() {
	vec4 color = subpassLoad(u_input_color).rgba;
	float depth = subpassLoad(u_input_depth).r;
	float linear_depth = linearize_depth(depth);
	out_col = compute_color(color, linear_depth);
}
