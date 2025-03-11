#version 450
#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(vertex)
#include "common.glsl"

layout (location = 0) out vec2 o_uv;

layout (set = 0, binding = 0) uniform readonly BUFFER(GlobalUniforms, global);
layout (set = 1, binding = 0) uniform readonly BUFFER(RingsUniforms, rings);

void main() {
	vec2[4] verts = vec2[4](
		vec2(-1.0, -1.0),
		vec2( 1.0, -1.0),
		vec2( 1.0,  1.0),
		vec2(-1.0,  1.0)
	);
	uint[6] indices = uint[6](0, 1, 2, 2, 3, 0);
	vec2 vtx = verts[indices[gl_VertexIndex]];
	o_uv = vtx * 0.5 + 0.5;
	gl_Position = rings.proj * rings.model_view * vec4(vtx.x, 0.0, vtx.y, 1.0);
	// gl_Position = vec4(vtx, 0.0, 1.0);
}
