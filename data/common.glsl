#ifndef COMMON_GLSLI_
#define COMMON_GLSLI_

const float MAX_FLOAT = 3.402823466e+38;
const float PI = 3.14159265358979;

#define BUFFER(S, name) _Uniform_##S { S name; }
#define SAMPLER(T, name) T name
#define _1D sampler1D
#define _2D sampler2D
#define _3D sampler3D
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
};

struct StaticMeshUniforms {
	mat4 proj;
	mat4 model_view;
	mat4 model;
};

struct MaterialUniforms {
	vec4 color;
	vec3 view_dir;
	float smoothness;
};

#endif // COMMON_GLSLI_
