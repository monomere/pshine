#extension GL_ARB_shading_language_include: enable
#pragma shader_stage(fragment)
#include "common.glsl"

layout (location = 0) out vec4 o_color;
struct Vec4 {
	vec4 v;
};
layout (push_constant) uniform BUFFER(Vec4, u_consts);

void main() {
	o_color = vec4(u_consts.v.rgb * u_consts.v.w, 0.0);
}
