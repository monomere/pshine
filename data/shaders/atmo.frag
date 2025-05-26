#version 460
#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(fragment)
#include "common.glsl"
#include "atmo_common.glsl"

layout (set = 0, binding = 0) uniform readonly BUFFER(GlobalUniforms, global);
layout (set = 2, binding = 0) uniform readonly BUFFER(AtmosphereUniforms, atmo);
layout (set = 2, binding = 3) uniform SAMPLER(_2D, atmo_lut);

layout (location = 0) out vec4 o_col;
layout (location = 0) in vec2 i_uv;
layout (input_attachment_index = 0, set = 2, binding = 1) uniform SUBPASS_INPUT(u_input_color);
layout (input_attachment_index = 1, set = 2, binding = 2) uniform SUBPASS_INPUT(u_input_depth);

// modified from https://www.shadertoy.com/view/lslXDr thank you GLtracy


// (3(1-g)/8π(2 + g))*((1 + c)/(1 + g - 2gc))

// Mie (g ∈ (-0.75, -0.999))
//       3(1 - g²)            1 + c²
// F = ------------- × -------------------
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
// F = 3/16π × (1 + c²)
float phase_ray(float cc) {
	return (3.0/(16.0 * PI)) * (1.0 + cc);
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

vec4 compute_atmo_params_baked(vec3 ray_origin) {
	vec3 ray_dir = atmo.sun.xyz;
	vec3 local_ray_origin = ray_origin - atmo.planet.xyz;
	float h = max(length(local_ray_origin) - atmo.planet.w, 0.0) / (atmo.radius - atmo.planet.w);
	vec2 d = exp(-h * vec2(atmo.coefs_ray.w, atmo.coefs_mie.w)) * (1.0 - h);
	vec4 p = texture(atmo_lut, vec2(dot(-normalize(local_ray_origin), ray_dir) * 0.5 + 0.5, h));
	return vec4(d, p.xy);
}

vec4 compute_atmo_params_slow(vec3 ray_origin) {
	vec3 ray_dir = atmo.sun.xyz;
	float ray_len = intersect_ray_sphere(atmo.planet.xyz, atmo.radius, ray_origin, ray_dir).y;
	vec3 local_ray_origin = ray_origin - atmo.planet.xyz;
	float h = max(length(local_ray_origin) - atmo.planet.w, 0.0) / (atmo.radius - atmo.planet.w);
	vec2 d = exp(-h * vec2(atmo.coefs_ray.w, atmo.coefs_mie.w)) * (1.0 - h);
	float n_ray = compute_optical_depth_slow(ray_origin, ray_dir, ray_len, atmo.coefs_ray.w);
	float n_mie = compute_optical_depth_slow(ray_origin, ray_dir, ray_len, atmo.coefs_mie.w);
	return vec4(d, n_ray, n_mie);
}

#define ATMO_SLOW 0

vec4 compute_atmo_params(vec3 current) {
#if defined(ATMO_SLOW) && ATMO_SLOW
	return compute_atmo_params_slow(current);
#else
	return compute_atmo_params_baked(current);
#endif
}

vec3 compute_light(vec3 ray_origin, vec3 ray_dir, float ray_len, vec3 col) {
	vec3 sum_ray = vec3(0.0);
	vec3 sum_mie = vec3(0.0);

	vec2 opt = vec2(0.0);

	float step_len = ray_len / float(atmo.scatter_point_samples);
	vec3 p = ray_origin;

	for (uint i = 0; i < atmo.scatter_point_samples; ++i) {
		vec4 params = compute_atmo_params(p);

		opt += params.xy * step_len;

		vec3 trans = exp(
			-(opt.x + params.z) * atmo.coefs_ray.xyz
			-(opt.y + params.w) * atmo.coefs_mie.x * atmo.coefs_mie.y
		);

		sum_ray += params.x * step_len * trans;
		sum_mie += params.y * step_len * trans;
		p += ray_dir * step_len;
	}

	float c = dot(ray_dir, global.sun.xyz);
	float cc = c * c;
	vec3 scatter
		= sum_ray * atmo.coefs_ray.xyz * phase_ray(cc);
		+ sum_mie * atmo.coefs_mie.x * phase_mie(atmo.coefs_mie.z, c, cc)
		;
	vec3 opacity = exp(-(atmo.coefs_ray.xyz * opt.x + atmo.coefs_mie.x * opt.y));
	return scatter * atmo.intensity + col * opacity;
}

vec4 compute_color(vec2 uv, vec4 col, float depth) {
	vec3 ray_origin = atmo.camera.xyz;
	vec2 ray_ndc = vec2(uv.x, 1.0 - uv.y) * 2.0 - 1.0;
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

	if (dst_to_atmo * atmo.scale_factor * 0.5 > depth) return col;

	// return vec4(vec3(dst_to_surface * 0.1), 1.0);

	float dst_thru_atmo = min(atmo_hit.y, dst_to_surface - dst_to_atmo);
	dst_thru_atmo = min(dst_thru_atmo, depth / atmo.scale_factor / 0.5);
	// if (distance(ray_origin, atmo.planet.xyz) < atmo.planet.w) return vec4(1.0, 0.0, 0.0, 1.0);

	if (dst_thru_atmo > 0.0) {
		const float epsilon = 0.00001;
		vec3 ray_origin_atmo = ray_origin + ray_dir * (dst_to_atmo + epsilon);
		vec3 light = compute_light(ray_origin_atmo, ray_dir, dst_thru_atmo - epsilon * 2.0, col.rgb);

		// float cos_sun_ad = cos(0.5);
		// float sundisk = smoothstep(cos_sun_ad, cos_sun_ad + 0.00002, cosTheta);
		// vec3 L0 = vec3(0.1) * Fex;
		// L0 += sunE * 19000.0 * Fex * sundisk;
		// vec3 texColor = Lin + L0;
		// texColor *= 0.04;
		// texColor += vec3(0.0, 0.001, 0.0025) * 0.3;

		return vec4(light, 1.0);
	}

	return col;
}

float linearize_depth(float depth) {
	// return depth;
	// return 1.0 - depth;
	return global.camera.w / depth;
}

void main() {
	vec4 color = subpassLoad(u_input_color).rgba;
	// o_col = color;
	float depth = subpassLoad(u_input_depth).r;
	float linear_depth = linearize_depth(depth);
	// o_col = vec4(vec3(linear_depth), 1.0);
	o_col = compute_color(i_uv, color, linear_depth);
}
