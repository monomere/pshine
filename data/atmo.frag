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
// F = 3/16 × π × (1 + c²)
float phase_ray(float cc) {
	return (3.0/16.0 * PI) * (1.0 + cc);
}

float compute_optical_depth_slow(vec3 ray_origin, vec3 ray_dir, float ray_len, float falloff) {
	float step_len = ray_len / float(atmo.optical_depth_samples);
	vec3 p = ray_origin; //  * step_len * 0.5;

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

#define RAY_BETA vec3(5.5e-6, 13.0e-6, 22.4e-6) /* rayleigh, affects the color of the sky */
#define MIE_BETA vec3(21e-6) /* mie, affects the color of the blob around the sun */
#define AMBIENT_BETA vec3(0.0) /* ambient, affects the scattering color when there is no lighting from the sun */
#define ABSORPTION_BETA vec3(2.04e-5, 4.97e-5, 1.95e-6) /* what color gets absorbed by the atmosphere (Due to things like ozone) */
#define G 0.7 /* mie scattering direction, or how big the blob around the sun is */
// and the heights (how far to go up before the scattering has no effect)
#define HEIGHT_RAY 1.3333 /* rayleigh height */
#define HEIGHT_MIE 0.2 /* and mie */
#define HEIGHT_ABSORPTION 5.0 /* at what height the absorption is at it's maximum */
#define ABSORPTION_FALLOFF 4e3 /* how much the absorption decreases the further away it gets from the maximum height */

vec3 calculate_scattering(vec3 ray_origin, vec3 ray_dir, float ray_len, vec3 col) {
	// add an offset to the camera position, so that the atmosphere is in the correct position
	// calculate the start and end position of the ray, as a distance along the ray
	// we do this with a ray sphere intersect
	float a = dot(ray_dir, ray_dir);
	float b = 2.0 * dot(ray_dir, ray_origin);
	float c = dot(ray_origin, ray_origin) - (atmo.radius * atmo.radius);
	float d = (b * b) - 4.0 * a * c;

	// stop early if there is no intersect
	if (d < 0.0) return col;

	bool allow_mie = false;
	// make sure the ray is no longer than allowed
	// get the step size of the ray
	float step_size_i = (ray_len) / float(atmo.scatter_point_samples);

	// next, set how far we are along the ray, so we can calculate the position of the sample
	// if the camera is outside the atmosphere, the ray should start at the edge of the atmosphere
	// if it's inside, it should start at the position of the camera
	// the min statement makes sure of that
	float ray_pos_i = 0;

	// these are the values we use to gather all the scattered light
	vec3 total_ray = vec3(0.0); // for rayleigh
	vec3 total_mie = vec3(0.0); // for mie

	// initialize the optical depth. This is used to calculate how much air was in the ray
	vec3 opt_i = vec3(0.0);

	// also init the scale height, avoids some vec2's later on
	vec2 scale_height = vec2(HEIGHT_RAY, HEIGHT_MIE);

	// Calculate the Rayleigh and Mie phases.
	// This is the color that will be scattered for this ray
	// mu, mumu and gg are used quite a lot in the calculation, so to speed it up, precalculate them
	float mu = dot(ray_dir, -global.sun.xyz);
	float mumu = mu * mu;
	float gg = G * G;
	float phase_ray = 3.0 / (50.2654824574 /* (16 * pi) */) * (1.0 + mumu);
	float phase_mie = allow_mie ? 3.0 / (25.1327412287 /* (8 * pi) */) * ((1.0 - gg) * (mumu + 1.0)) / (pow(1.0 + gg - 2.0 * mu * G, 1.5) * (2.0 + gg)) : 0.0;

	// now we need to sample the 'primary' ray. this ray gathers the light that gets scattered onto it
	for (int i = 0; i < atmo.scatter_point_samples; ++i) {
			
			// calculate where we are along this ray
			vec3 pos_i = ray_origin + ray_dir * ray_pos_i;
			
			// and how high we are above the surface
			float height_i = length(pos_i) - atmo.planet.w;
			
			// now calculate the density of the particles (both for rayleigh and mie)
			vec3 density = vec3(exp(-height_i / scale_height), 0.0);
			
			// and the absorption density. this is for ozone, which scales together with the rayleigh, 
			// but absorbs the most at a specific height, so use the sech function for a nice curve falloff for this height
			// clamp it to avoid it going out of bounds. This prevents weird black spheres on the night side
			float denom = (HEIGHT_ABSORPTION - height_i) / ABSORPTION_FALLOFF;
			density.z = (1.0 / (denom * denom + 1.0)) * density.x;
			
			// multiply it by the step size here
			// we are going to use the density later on as well
			density *= step_size_i;
			
			// Add these densities to the optical depth, so that we know how many particles are on this ray.
			opt_i += density;
			
			// Calculate the step size of the light ray.
			// again with a ray sphere intersect
			// a, b, c and d are already defined
			a = dot(global.sun.xyz, global.sun.xyz);
			b = 2.0 * dot(global.sun.xyz, pos_i);
			c = dot(pos_i, pos_i) - (atmo.radius * atmo.radius);
			d = (b * b) - 4.0 * a * c;

			// no early stopping, this one should always be inside the atmosphere
			// calculate the ray length
			float step_size_l = (-b + sqrt(d)) / (2.0 * a * float(atmo.optical_depth_samples));

			// and the position along this ray
			// this time we are sure the ray is in the atmosphere, so set it to 0
			float ray_pos_l = step_size_l * 0.5;

			// and the optical depth of this ray
			vec3 opt_l = vec3(0.0);
					
			// now sample the light ray
			// this is similar to what we did before
			for (int l = 0; l < atmo.optical_depth_samples; ++l) {

					// calculate where we are along this ray
					vec3 pos_l = pos_i + global.sun.xyz * ray_pos_l;

					// the heigth of the position
					float height_l = length(pos_l) - atmo.planet.w;

					// calculate the particle density, and add it
					// this is a bit verbose
					// first, set the density for ray and mie
					vec3 density_l = vec3(exp(-height_l / scale_height), 0.0);
					
					// then, the absorption
					float denom = (HEIGHT_ABSORPTION - height_l) / ABSORPTION_FALLOFF;
					density_l.z = (1.0 / (denom * denom + 1.0)) * density_l.x;
					
					// multiply the density by the step size
					density_l *= step_size_l;
					
					// and add it to the total optical depth
					opt_l += density_l;
					
					// and increment where we are along the light ray.
					ray_pos_l += step_size_l;
					
			}
			
			// Now we need to calculate the attenuation
			// this is essentially how much light reaches the current sample point due to scattering
			vec3 attn = exp(-RAY_BETA * (opt_i.x + opt_l.x) - MIE_BETA * (opt_i.y + opt_l.y) - ABSORPTION_BETA * (opt_i.z + opt_l.z));

			// accumulate the scattered light (how much will be scattered towards the camera)
			total_ray += density.x * attn;
			total_mie += density.y * attn;

			// and increment the position on this ray
			ray_pos_i += step_size_i;
		
	}

	// calculate how much light can pass through the atmosphere
	vec3 opacity = exp(-(MIE_BETA * opt_i.y + RAY_BETA * opt_i.x + ABSORPTION_BETA * opt_i.z));

	// calculate and return the final color
	return (
		phase_ray * RAY_BETA * total_ray // rayleigh color
		+ phase_mie * MIE_BETA * total_mie // mie
		+ opt_i.x * AMBIENT_BETA // and ambient
	) * 40 + col * opacity; // now make sure the background is rendered correctly
}

vec3 compute_light(vec3 ray_origin, vec3 ray_dir, float ray_len, vec3 col) {
	vec3 sum_ray = vec3(0.0);
	vec3 sum_mie = vec3(0.0);

	vec2 opt = vec2(0.0);

	float step_len = ray_len / float(atmo.scatter_point_samples);
	vec3 p = ray_origin;

	for (uint i = 0; i < atmo.scatter_point_samples; ++i) {
		float sun_ray_len = intersect_ray_sphere(atmo.planet.xyz, atmo.radius, p, global.sun.xyz).y;
		vec4 params = compute_atmo_params(p, global.sun.xyz, sun_ray_len);

		opt += params.xy * step_len;

		vec3 trans = exp(
			-(opt.x + params.z) * atmo.coefs_ray.xyz
			// -(opt.y + params.w) * atmo.coefs_mie.x * atmo.coefs_mie.y
		);

		sum_ray += params.x * step_len * trans;
		// sum_mie += params.y * step_len * trans;
		p += ray_dir * step_len;
	}

	float c = dot(ray_dir, -global.sun.xyz);
	// float mu = dot(ray_dir, -global.sun.xyz);
	// float mumu = mu * mu;
	// float gg = 0.7 * 0.7;
	// float phase_ray = 3.0 / (50.2654824574 /* (16 * pi) */) * (1.0 + mumu);
	vec3 scatter
		= sum_ray * atmo.coefs_ray.xyz * phase_ray(c * c)
		// + sum_mie * atmo.coefs_mie.x * phase_mie(atmo.coefs_mie.z, c, c * c)
		;
	return scatter;
	// vec3 opacity = exp(-atmo.coefs_ray.xyz * opt.x);

	// return (
	// 	phase_ray * atmo.coefs_ray.xyz * sum_ray // rayleigh color
	// ) * 40 + col * opacity; // now make sure the background is rendered correctly
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
	// if (distance(ray_origin, atmo.planet.xyz) < atmo.planet.w) return vec4(1.0, 0.0, 0.0, 1.0);

	if (dst_thru_atmo > 0.0) {
		const float epsilon = 0.00001;
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
