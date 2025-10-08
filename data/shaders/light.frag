#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(fragment)
#include "common.glsl"

layout (location = 0) out vec4 o_col;
layout (location = 0) in vec2 i_uv;

layout (input_attachment_index = 0, set = 0, binding = 0) uniform SUBPASS_INPUT(u_color0);
layout (input_attachment_index = 1, set = 0, binding = 1) uniform SUBPASS_INPUT(u_depth);
layout (input_attachment_index = 2, set = 0, binding = 2) uniform SUBPASS_INPUT(u_diffuse_o);
layout (input_attachment_index = 3, set = 0, binding = 3) uniform SUBPASS_INPUT(u_normal_r_m);
layout (input_attachment_index = 4, set = 0, binding = 4) uniform SUBPASS_INPUT(u_emissive_s);
layout (set = 0, binding = 5) uniform readonly BUFFER(GlobalUniforms, u_global);

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

vec3 fresnel_schlick(float cos_theta, vec3 F0) {
	return F0 + (1.0 - F0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

vec2 sign_not_zero(vec2 v) {
	return vec2(
		(v.x >= 0.0) ? +1.0 : -1.0,
		(v.y >= 0.0) ? +1.0 : -1.0
	);
}

vec3 oct_to_float32x3(vec2 e) {
	vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
	if (v.z < 0) v.xy = (1.0 - abs(v.yx)) * sign_not_zero(v.xy);
	return normalize(v);
}
  
vec3 _vis(vec3 x) {
	return clamp(abs(x), 0.0, 1.0);
}

vec3 view_pos_from_depth(float depth) {
	if (depth <= 0.0001) return vec3(0, 0, 0);
	vec4 ndc_pos = vec4(i_uv * 2.0 - 1.0, depth, 1.0);
	vec4 clip_pos = u_global.inv_proj * ndc_pos;
	return clip_pos.xyz / clip_pos.w;
}

vec3 world_pos_from_depth(float depth) {
	vec4 view_pos = vec4(view_pos_from_depth(depth), 1.0);
	return vec3(u_global.inv_view * view_pos);
}

vec3 gbuffer_light() {
	vec4 i_color0 = subpassLoad(u_color0).rgba;
	vec4 i_diffuse_o = subpassLoad(u_diffuse_o).rgba;
	vec3 bg_color = (1.0 - float(any(greaterThan(i_diffuse_o.rgb, vec3(0))))) * i_color0.rgb;
	vec4 i_normal_r_m = subpassLoad(u_normal_r_m).rgba;
	vec4 i_emissive_s = subpassLoad(u_emissive_s).rgba;
	float i_depth = subpassLoad(u_depth).r;
	float shadow_factor = clamp(i_emissive_s.a, 0.1, 1.0);
	return vec3(shadow_factor);
	// return vec3(shadow_factor);
	// float i_shadow = texture(u_shadow, i_uv).r;
	// vec3 i_shadowc = texture(u_shadow, i_uv).rgb;
	// return i_shadowc;
	// return vec3(abs(i_depth) * 10000);
	// float shadow_factor = float(i_shadow > i_depth);

	vec3 emissive = i_emissive_s.rgb;
	vec3 normal_map = oct_to_float32x3(i_normal_r_m.rg);

	vec3 albedo = i_diffuse_o.rgb;
	float occlusion = i_diffuse_o.a;
	float roughness = i_normal_r_m.b;
	float metallic = i_normal_r_m.a;

	vec3 k_pos = world_pos_from_depth(i_depth);
	// return vec3(sign(k_pos) * 0.5 + 0.5);
	vec3 k_normal = normal_map;
	vec3 k_cam_pos = vec3(0.0); // u_global.camera.xyz
	vec3 k_sun_dir = u_global.sun.xyz;

	// return k_normal;
	// return albedo * clamp(dot(k_sun_dir, k_normal), 0.0, 1.0);

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
		vec3 radiance = vec3(8.0) * shadow_factor; // lightColors[i] * attenuation;

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
		// note that we already multiplied the BRDF by the Fresnel (kS) so we won't multiply by kS again
		Lo += (kD * albedo / PI + specular) * radiance * NdotL;
	}
	// o_color = _vis(k_normal);
	return Lo * metallic + emissive * 16.0 + bg_color;
}

void main() {
	o_col = vec4(gbuffer_light(), 1.0);
}
