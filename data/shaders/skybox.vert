#version 450
#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(vertex)
#include "common.glsl"

layout (location = 0) out vec3 o_pos;

layout (push_constant) uniform BUFFER(SkyboxConsts, skybox);

void main() {
	vec3[8] verts = vec3[8](
		vec3( 1.0,  1.0,  1.0),
		vec3(-1.0,  1.0,  1.0),
		vec3( 1.0,  1.0, -1.0),
		vec3(-1.0,  1.0, -1.0),
		vec3( 1.0, -1.0,  1.0),
		vec3(-1.0, -1.0,  1.0),
		vec3(-1.0, -1.0, -1.0),
		vec3( 1.0, -1.0, -1.0)
	);
	uint[14] indices = uint[14](
		3, 2, 6, 7, 4, 2, 0,
		3, 1, 6, 5, 4, 1, 0
	);
	
	vec3 vtx = verts[indices[gl_VertexIndex]] * 1.0;
	o_pos = vtx;
	gl_Position = skybox.proj * skybox.view * vec4(vtx, 1.0);
	gl_Position.z = 0.0; // since we use a reversed-z buffer, 0.0 is like w.
}
