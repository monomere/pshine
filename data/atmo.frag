#version 450

layout (location = 0) out vec4 out_col;

layout (location = 0) in vec2 i_uv;

layout (std140, binding = 0) uniform ATMO_UNIFORMS {
	
} atmo;

// vec2 intersect_ray_sphere(
// 	vec3 sphere_center,
// 	float sphere_radius,
// 	vec3 ray_origin,
// 	vec3 ray_direction
// ) {
// 	vec3 local_ray_origin = ray_origin - sphere_center;
// 	float b = 2 * dot(local_ray_origin, ray_direction);
// 	float c
// 		= dot(local_ray_origin, local_ray_origin)
// 		- sphere_radius * sphere_radius;
// 	float d = b * b - 4.0 * c;

// 	if (d > 0.0) {
// 		float s = sqrt(d);
// 		float near = max(0.0, (-b - s) / 2.0);
// 		float far = (-b + s) / 2.0;
// 		if (far >= 0.0) return vec2(near, far - near);
// 	}

// 	return vec2(1.0 / 0.0, 0.0);
// }

// float compute_local_density(vec3 pos) {
// 	float height = distance(ubo.planet.center, pos) - ubo.planet.radius;
// 	float height01 = height / (ubo.atmosphere.radius + ubo.planet.radius);
// 	return exp(-height01 * ubo.atmosphere.density_falloff) * (1 - height01);
// }

// float compute_optical_depth(
// 	vec3 ray_origin,
// 	vec3 ray_direction,
// 	float ray_length
// ) {
// 	vec3 current = ray_origin;
// 	float total = 0.0;
// 	float ray_step_length = ray_length / (ubo.atmosphere.optical_depth_samples - 1);
// 	for (int i = 0; i < ubo.atmosphere.optical_depth_samples; ++i) {
// 		float local_density = compute_local_density(current);
// 		total += local_density * ray_step_length;
// 		current += ray_direction * ray_step_length;
// 	}
// 	return total;
// }

// float compute_light(
// 	vec3 ray_origin,
// 	vec3 ray_direction,
// 	float ray_length
// ) {
// 	vec3 current = ray_origin;
// 	float ray_step_length = ray_length / (ubo.atmosphere.scatter_point_samples - 1);
// 	float total = 0.0;
// 	for (int i = 0; i < ubo.atmosphere.scatter_point_samples; ++i) {
// 		float sun_ray_len = intersect_ray_sphere(
// 			ubo.planet.center,
// 			ubo.atmosphere.radius,
// 			current,
// 			-ubo.world.sun_direction
// 		).y;
// 		float sun_ray_optical_depth = compute_optical_depth(
// 			current,
// 			-ubo.world.sun_direction,
// 			sun_ray_len
// 		);
// 		float view_ray_optical_depth = compute_optical_depth(
// 			current,
// 			-ray_direction,
// 			ray_step_length * i
// 		);
// 		float transmittance = exp(-(sun_ray_optical_depth + view_ray_optical_depth));
// 		float local_density = compute_local_density(current);
// 		total += local_density * transmittance * ray_step_length;
// 		current += ray_direction * ray_step_length;
// 	}
// 	return total;
// }

// vec4 compute_color(vec4 col, float depth) {
// 	vec2 ray_uv = ubo.camera.plane * (v_in.uv * 2.0 - 1.0);

// 	vec3 ray_target
// 		= cross(ubo.camera.up, ubo.camera.right) * ubo.camera.znear
// 		+ ubo.camera.right * ray_uv.x
// 		+ ubo.camera.up * ray_uv.y;

// 	vec3 ray_origin = ubo.camera.pos;
// 	vec3 ray_direction = normalize(ray_origin - ray_target);

// 	float distance_to_surface = intersect_ray_sphere(
// 		ubo.planet.center,
// 		ubo.planet.radius,
// 		ray_origin,
// 		ray_direction
// 	).x;

// 	vec2 hit = intersect_ray_sphere(
// 		ubo.planet.center,
// 		ubo.atmosphere.radius,
// 		ray_origin,
// 		ray_direction
// 	);

// 	float distance_to_atmosphere = hit.x;
// 	float distance_through_atmosphere = min(
// 		hit.y,
// 		distance_to_surface - distance_to_atmosphere
// 	);

// 	if (distance_through_atmosphere > 0.0) {
// 		return vec4(vec3(
// 			distance_through_atmosphere / (2.0 * ubo.atmosphere.radius
// 		)), 1.0);
// 		// vec3 ray_org_atmo = ray_org + ray_dir * dst_to_atmo;
// 		// float light = compute_light(ray_org_atmo, ray_dir, dst_thru_atmo);
// 		// return col * (1 - light) + light;
// 	}

// 	return col;
// }

float linearize_depth(float depth) {
	float n = ubo.camera.znear;
	float f = ubo.camera.zfar;
	float z = depth;
	return (2.0 * n) / (f + n - z * (f - n));	
}

layout (input_attachment_index = 0, binding = 1) uniform subpassInput u_input_color;
layout (input_attachment_index = 1, binding = 2) uniform subpassInput u_input_depth;

void main() {
	vec4 color = subpassLoad(u_input_color).rgba;
	float depth = subpassLoad(u_input_depth).a;
	out_col = compute_color(color, linearize_depth(depth));
}
