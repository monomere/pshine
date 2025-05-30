#version 450
#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(fragment)
#include "common.glsl"
#include "noise.glsl"

layout (location = 0) out vec4 o_color;

layout (location = 0) in vec3 i_position;
layout (location = 1) in vec3 i_normal;
layout (location = 2) in vec2 i_texcoord;

// layout (location = 3) in mat3 i_tbn;
layout (location = 3) in vec3 i_tangent_sun_dir;
layout (location = 4) in vec3 i_tangent_cam_pos;
layout (location = 5) in vec3 i_tangent_pos;
layout (location = 6) in vec3 i_tangent_normal;

layout (set = 0, binding = 0) uniform readonly BUFFER(GlobalUniforms, global);
layout (set = 1, binding = 0) uniform readonly BUFFER(StdMaterialUniforms, material);
layout (set = 2, binding = 0) uniform readonly BUFFER(StdMeshUniforms, mesh);
layout (set = 1, binding = 1) uniform SAMPLER(_2D, texture_diffuse);
layout (set = 1, binding = 2) uniform SAMPLER(_2D, texture_ao_rough_metal);
layout (set = 1, binding = 3) uniform SAMPLER(_2D, texture_normal);
layout (set = 1, binding = 4) uniform SAMPLER(_2D, texture_emissive);

vec3 fresnel_schlick(float cos_theta, vec3 F0) {
	return F0 + (1.0 - F0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

float distribution_ggx(vec3 N, vec3 H, float roughness) {
	float a      = roughness*roughness;
	float a2     = a*a;
	float NdotH  = max(dot(N, H), 0.0);
	float NdotH2 = NdotH*NdotH;

	float num   = a2;
	float denom = (NdotH2 * (a2 - 1.0) + 1.0);
	denom = PI * denom * denom;

	return num / denom;
}

float geometry_schlick_ggx(float NdotV, float roughness) {
	float r = (roughness + 1.0);
	float k = (r*r) / 8.0;

	float num   = NdotV;
	float denom = NdotV * (1.0 - k) + k;

	return num / denom;
}

float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness) {
	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float ggx2  = geometry_schlick_ggx(NdotV, roughness);
	float ggx1  = geometry_schlick_ggx(NdotL, roughness);

	return ggx1 * ggx2;
}

vec4 _vis(vec3 x) {
	return vec4(clamp(abs(x), 0.0, 1.0), 1.0);
}

void main() {
	vec3 col = texture(texture_diffuse, i_texcoord).rgb;
	vec3 ao_rough_metal = texture(texture_ao_rough_metal, i_texcoord).rgb;
	vec3 emissive = texture(texture_emissive, i_texcoord).rgb;
	vec3 normal_map = -normalize(texture(texture_normal, i_texcoord).rgb * 2.0 - 1.0);

	vec3 albedo = col;
	float occlusion = ao_rough_metal.r;
	float roughness = ao_rough_metal.g;
	float metallic = ao_rough_metal.b;

	vec3 k_pos = i_tangent_pos;
	vec3 k_normal = normal_map;
	vec3 k_cam_pos = i_tangent_cam_pos;
	vec3 k_sun_dir = i_tangent_sun_dir;

	// vec3 k_pos = i_position;
	// vec3 k_cam_pos = mesh.rel_cam_pos.xyz;
	// vec3 k_sun_dir = -mesh.sun.xyz;
	// vec3 k_normal = i_tbn * normal_map;

	// float shadow = clamp(dot(normalize(k_sun_dir), k_normal), 0.06, 1.0);

	vec3 N = k_normal;
	vec3 V = normalize(k_cam_pos - k_pos);
	// calculate reflectance at normal incidence; if dia-electric (like plastic) use F0 
	// of 0.04 and if it's a metal, use the albedo color as F0 (metallic workflow)    
	vec3 F0 = vec3(0.04); 
	F0 = mix(F0, albedo, metallic);

	// reflectance equation
	vec3 Lo = vec3(0.0);
	{
		// calculate per-light radiance
		vec3 L = normalize(k_sun_dir);
		vec3 H = normalize(V + L);
		// float distance = length(k_sun_dir);
		// float attenuation = 1.0; // 1.0 / (distance * distance);
		vec3 radiance = vec3(4.0); // lightColors[i] * attenuation;

		// Cook-Torrance BRDF
		float NDF = distribution_ggx(N, H, roughness);
		float G   = geometry_smith(N, V, L, roughness);      
		vec3 F    = fresnel_schlick(max(dot(H, V), 0.0), F0);
				
		vec3 numerator    = NDF * G * F; 
		float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001; // + 0.0001 to prevent divide by zero
		vec3 specular = numerator / denominator;
		
		// kS is equal to Fresnel
		vec3 kS = F;
		// for energy conservation, the diffuse and specular light can't
		// be above 1.0 (unless the surface emits light); to preserve this
		// relationship the diffuse component (kD) should equal 1.0 - kS.
		vec3 kD = vec3(1.0) - kS;
		// multiply kD by the inverse metalness such that only non-metals 
		// have diffuse lighting, or a linear blend if partly metal (pure metals
		// have no diffuse light).
		kD *= 1.0 - metallic;	  

		// scale light by NdotL
		float NdotL = max(dot(N, L), 0.0);        

		// add to outgoing radiance Lo
		Lo += (kD * albedo / PI + specular) * radiance * NdotL;  // note that we already multiplied the BRDF by the Fresnel (kS) so we won't multiply by kS again
	}
	// o_color = _vis(k_normal);
	o_color = vec4(Lo * ao_rough_metal.r + emissive * 16.0, 1.0);
}
