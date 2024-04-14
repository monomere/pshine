#version 460
#pragma shader_stage(fragment)
#include "common.glsli"
#include "atmo_common.glsli"

layout (set = 0, binding = 0) uniform readonly BUFFER(GlobalUniforms, global);
layout (set = 2, binding = 0) uniform readonly BUFFER(AtmosphereUniforms, atmo);
layout (set = 2, binding = 3) uniform sampler2D atmo_lut;

layout (location = 0) out vec4 o_col;
layout (location = 0) in vec2 i_uv;
layout (input_attachment_index = 0, set = 2, binding = 1) uniform subpassInput u_input_color;
layout (input_attachment_index = 1, set = 2, binding = 2) uniform subpassInput u_input_depth;

// modified from https://www.shadertoy.com/view/lslXDr thank you GLtracy

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

float compute_optical_depth_slow(vec3 ray_origin, vec3 ray_dir, float ray_len, float falloff) {
	float step_len = ray_len / float(atmo.optical_depth_samples);
	vec3 p = ray_origin;

	float sum = 0.0;
	for (uint i = 0; i < atmo.optical_depth_samples; ++i, p += ray_dir * step_len) {
		sum += compute_density(p - atmo.planet.xyz, falloff, atmo.planet.w, atmo.radius - atmo.planet.w);
	}

	return sum * step_len;
}

vec4 compute_atmo_params_baked(vec3 ray_origin, vec3 ray_dir, float ray_len) {
	vec3 local_ray_origin = ray_origin - atmo.planet.xyz;
	float h = max(length(local_ray_origin) - atmo.planet.w, 0.0) / (atmo.radius - atmo.planet.w);
	vec2 d = exp(-h * vec2(atmo.coefs_ray.w, atmo.coefs_mie.w)) * (1.0 - h);
	vec4 p = texture(atmo_lut, vec2(dot(-normalize(local_ray_origin), ray_dir) * 0.5 + 0.5, h));
	return vec4(d, p.xy);
}

vec4 compute_atmo_params_slow(vec3 current, vec3 ray_dir, float ray_len) {
	float d_ray = compute_density(current - atmo.planet.xyz, atmo.coefs_ray.w, atmo.planet.w, atmo.radius - atmo.planet.w);
	float d_mie = compute_density(current - atmo.planet.xyz, atmo.coefs_mie.w, atmo.planet.w, atmo.radius - atmo.planet.w);
	float n_ray = compute_optical_depth_slow(current, ray_dir, ray_len, atmo.coefs_ray.w);
	float n_mie = compute_optical_depth_slow(current, ray_dir, ray_len, atmo.coefs_mie.w);
	return vec4(d_ray, d_mie, n_ray, n_mie);
}


vec4 compute_atmo_params(vec3 current, vec3 ray_dir, float ray_len) {
	return compute_atmo_params_baked(current, ray_dir, ray_len);
}

vec3 compute_light(vec3 ray_origin, vec3 ray_dir, float ray_len, vec3 col) {
	vec3 sum_ray = vec3(0.0);
	vec3 sum_mie = vec3(0.0);

	float n_ray0 = 0.0;
	float n_mie0 = 0.0;

	float step_len = ray_len / float(atmo.scatter_point_samples);
	vec3 p = ray_origin;

	for (uint i = 0; i < atmo.scatter_point_samples; ++i, p += ray_dir * step_len) {
		float sun_ray_len = intersect_ray_sphere(atmo.planet.xyz, atmo.radius, p, global.sun.xyz).y;
		vec4 params = compute_atmo_params(p, global.sun.xyz, sun_ray_len);

		n_ray0 += params.x * step_len;
		n_mie0 += params.y * step_len;

		vec3 trans = exp(
			-(n_ray0 + params.z) * atmo.coefs_ray.xyz
			-(n_mie0 + params.w) * atmo.coefs_mie.x * atmo.coefs_mie.y
		);

		sum_ray += params.x * step_len * trans;
		sum_mie += params.y * step_len * trans;
	}

	float c = dot(ray_dir, -global.sun.xyz);
	vec3 scatter
		= sum_ray * atmo.coefs_ray.xyz * phase_ray(c * c)
		+ sum_mie * atmo.coefs_mie.x * phase_mie(atmo.coefs_mie.z, c, c * c)
		;

	return col * (1.0 - scatter) + scatter * 10.0;
}

vec4 compute_color(vec2 uv, vec4 col, float depth) {
	vec3 ray_origin = global.camera.xyz;
	vec2 ray_ndc = vec2(uv.x, 1.0 - uv.y) * 2.0 - 1.0;
	// vec4 ray_clip = vec4(ray_ndc, 0.0, 1.0);
	// vec4 ray_eye =  global.proj * ray_clip;
	// vec4 ray_eye2 = vec4(ray_eye.xy, 1.0, 0.0);
	// vec3 ray_dir = normalize((inverse(global.view) * ray_eye).xyz);
	vec3 camera_forward = cross(global.camera_right.xyz, global.camera_up.xyz);
	vec3 ray_dir
		= camera_forward.xyz * global.camera.w
		+ global.camera_up.xyz * ray_ndc.y * global.camera_up.w
		+ global.camera_right.xyz * ray_ndc.x * global.camera_right.w;
	ray_dir = normalize(ray_dir);

	vec2 surface_hit = intersect_ray_sphere(atmo.planet.xyz, atmo.planet.w, ray_origin, ray_dir);

	float dst_to_surface = surface_hit.x;

	vec2 atmo_hit = intersect_ray_sphere(atmo.planet.xyz, atmo.radius, ray_origin, ray_dir);

	float dst_to_atmo = atmo_hit.x;

	// if (dst_to_atmo < depth) return col;

	float dst_thru_atmo = min(atmo_hit.y, dst_to_surface - dst_to_atmo);

	if (dst_thru_atmo > 0.0) {
		const float epsilon = 0.0001;
		vec3 ray_origin_atmo = ray_origin + ray_dir * (dst_to_atmo + epsilon);
		vec3 light = compute_light(ray_origin_atmo, ray_dir, dst_thru_atmo - epsilon * 2.0, col.rgb);

		// light = pow(light, vec3(1.0 / 2.2));
		return vec4(light, 1.0);
	}

	return col;
}

float linearize_depth(float depth) {
	return global.camera.w / (depth * 2 - 1);
}

void main() {
	vec4 color = subpassLoad(u_input_color).rgba;
	float depth = subpassLoad(u_input_depth).r;
	float linear_depth = linearize_depth(depth);
	o_col = compute_color(i_uv, color, linear_depth);
}
