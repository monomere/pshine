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
	float pad[3];
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

// modified from https://www.shadertoy.com/view/lslXDr thank you GLtracy

#define PI 3.14159265358979

// Mie (g ∈ (-0.75, -0.999))
//       3(1 - g²)            1 + c²
// F = ⎯⎯⎯⎯⎯⎯⎯⎯ × ⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯
//      8π(2 + g²)      (1 + g² - 2gc)¹⋅⁵
float phase_mie(float g, float c, float cc) {
	float gg = g * g;
	
	float a = (1.0 - gg) * (1.0 + cc);

	float b = 1.0 + gg - 2.0 * g * c;
	b *= sqrt(b);
	b *= 2.0 + gg;	
	
	return (3.0 / 8.0 / PI) * a / b;
}

// Rayleigh (g = 0)
// F = 3/16 × π × (1 + c²)
float phase_ray(float cc) {
	return (3.0 / 16.0 / PI) * (1.0 + cc);
}

float compute_density(vec3 pos, float falloff) {
	float h = max(length(pos) - atmo.planet.w, 0.0) / (atmo.radius - atmo.planet.w);
	return exp(-h * falloff) * (1.0 - h);
}

float compute_optical_depth(vec3 ray_origin, vec3 ray_dir, float ray_len, float falloff) {
	float step_len = ray_len / float(atmo.optical_depth_samples);
	vec3 p = ray_origin;

	float sum = 0.0;
	for (uint i = 0; i < atmo.optical_depth_samples; ++i, p += ray_dir * step_len) {
		sum += compute_density(p, falloff);
	}

	return sum * step_len;
}

vec3 compute_light(vec3 ray_origin, vec3 ray_dir, float ray_len, vec3 col) {
	const float falloff_ray = 20.0;
	const float falloff_mie = 50.0;
	
	const vec3 k_ray = vec3(3.8, 13.5, 33.1);
	const vec3 k_mie = vec3(21.0);
	const float k_mie_ex = 1.1;
	
	vec3 sum_ray = vec3(0.0);
	vec3 sum_mie = vec3(0.0);
	
	float n_ray0 = 0.0;
	float n_mie0 = 0.0;
	
	float step_len = ray_len / float(atmo.scatter_point_samples);
	vec3 p = ray_origin;
    
	for (uint i = 0; i < atmo.scatter_point_samples; ++i, p += ray_dir * step_len) {   
		float density_ray = compute_density(p, falloff_ray) * step_len;
		float density_mie = compute_density(p, falloff_mie) * step_len;
			
		n_ray0 += density_ray;
		n_mie0 += density_mie;

		float sun_ray_len = intersect_ray_sphere(atmo.planet.xyz, atmo.radius, p, global.sun.xyz).y;

		float n_ray1 = compute_optical_depth(p, global.sun.xyz, sun_ray_len, falloff_ray);
		float n_mie1 = compute_optical_depth(p, global.sun.xyz, sun_ray_len, falloff_mie);

		vec3 trans = exp(-((n_ray0 + n_ray1) * k_ray + (n_mie0 + n_mie1) * k_mie * k_mie_ex));

		sum_ray += density_ray * trans;
		sum_mie += density_mie * trans;
	}

	float c = dot(ray_dir, -global.sun.xyz);
	vec3 scatter
		= sum_ray * k_ray * phase_ray(c * c)
		+ sum_mie * k_mie * phase_mie(-0.78, c, c * c);
	
	return scatter * 10.0; col * (1.0 - scatter) + scatter;
}

vec4 compute_color(vec4 col, float depth) {
	vec3 ray_origin = global.camera.xyz;
	vec2 ray_ndc = vec2(i_uv.x, i_uv.y) * 2.0 - 1.0;
	vec4 ray_clip = vec4(ray_ndc, 0.0, 1.0);
	vec4 ray_eye = inverse(global.proj) * ray_clip;
	vec4 ray_eye2 = vec4(ray_eye.xy, 1.0, 0.0);
	vec3 ray_dir = normalize((inverse(global.view) * ray_eye).xyz);

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

	// if (dst_to_atmo < depth) return col;

	float dst_thru_atmo = min(
		atmo_hit.y,
		dst_to_surface - dst_to_atmo
	);

	if (dst_thru_atmo > 0.0) {
		const float epsilon = 0.0001;
		vec3 ray_origin_atmo = ray_origin + ray_dir * (dst_to_atmo + epsilon);
		vec3 light = compute_light(
			ray_origin_atmo,
			ray_dir,
			dst_thru_atmo - epsilon * 2.0,
			col.rgb
		);

		// light = pow(light, vec3(1.0 / 2.2));
		return vec4(light, 1.0);
	}

	return col;
}

float linearize_depth(float depth) {
	return global.camera.w / (depth * 2 - 1);
}

layout (input_attachment_index = 0, set = 2, binding = 1) uniform subpassInput u_input_color;
layout (input_attachment_index = 1, set = 2, binding = 2) uniform subpassInput u_input_depth;

void main() {
	vec4 color = subpassLoad(u_input_color).rgba;
	float depth = subpassLoad(u_input_depth).r;
	float linear_depth = linearize_depth(depth);
	out_col = compute_color(color, linear_depth); // mix(, color, atmo.blend_factor);
}
