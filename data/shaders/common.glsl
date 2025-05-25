#ifndef COMMON_GLSLI_
#define COMMON_GLSLI_

const float MAX_FLOAT = 3.402823466e+38;
const float PI = 3.14159265358979;
const float EPSILON = 1.19e-07;

#define BUFFER(S, name) _Uniform_##S { S name; }
#define SAMPLER(T, name) T name
#define _1D sampler1D
#define _2D sampler2D
#define _3D sampler3D
#define _CUBE samplerCube
#define SUBPASS_INPUT(name) subpassInput name

struct GlobalUniforms {
	vec4 sun;
	vec4 camera;        // xyz, w=near_plane.z
	vec4 camera_right;  // xyz, w=near_plane.x
	vec4 camera_up;     // xyz, w=near_plane.y
};

struct AtmosphereUniforms {
	vec4 planet;     // xyz, w=radius
	vec4 coefs_ray;  // xyz=k_ray, w=falloff_ray
	vec4 coefs_mie;  // x=k_mie, y=k_mie_ext, z=g, w=falloff_mie
	vec4 camera;     // xyz, w=_
	float radius;
	uint optical_depth_samples;
	uint scatter_point_samples;
	float intensity;
	vec3 sun;
	float scale_factor; // scs_size / scale_factor = atmo_size.
};

struct StaticMeshUniforms {
	mat4 proj;
	mat4 model_view;
	mat4 model;
	vec4 sun;
};

struct StdMeshUniforms {
	mat4 proj;
	mat4 model_view;
	mat4 model;
	vec4 sun;
	vec3 rel_cam_pos;
};

struct MaterialUniforms {
	vec4 color;
	vec3 view_dir;
	float smoothness;
};

struct StdMaterialUniforms {
	vec3 view_dir;
};

struct RingsUniforms {
	mat4 proj;
	mat4 model_view;
	vec4 sun;
	float inner_radius;
	float outer_radius;
	float rel_planet_radius;
	float smoothing;
};

struct SkyboxConsts {
	mat4 proj;
	mat4 view;
};

struct GraphicsSettingsConsts {
	float bloom_threshold;
	float bloom_knee;
	float exposure;
	float camera_fov;
};

float luma_from_rgb(vec3 rgb) {
	return 0.2126 * rgb.r + 0.7152 * rgb.g + 0.0722 * rgb.b;
}

#endif // COMMON_GLSLI_
