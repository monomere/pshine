#version 460 core

#define CENTER vec3(0)
#define PLANET_RADIUS 1.0
#define ATMO_HEIGHT 1.0
#define SUN vec3(1, 0, 0)

#define IN_SCATTER_SAMPLES 10
#define OPTICAL_DEPTH_SAMPLES 10

#define F_RAY 10.0
#define K_RAY vec3(10.0, 20.0, 30.0)

const float FLT_INF = 3.402823466e+38;

// [[requires(length(d) == 1.0)]]
vec2 sphere(vec3 ro, vec3 rd, vec3 sc, float sr) {
    vec3 oc = ro - sc;
    float b = 2.0 * dot(oc, rd);
    float c = dot(oc, oc) - sr * sr;
    float d = b * b - 4.0 * c;
    if (d < 0.0) return vec2(FLT_INF, -FLT_INF);
    d = sqrt(d);
    return vec2(max((-b - d) * 0.5, 0.0), (-b + d) * 0.5);
}

float density(vec3 o, float k) {
    float h = max(distance(o, CENTER) - PLANET_RADIUS, 0.0) / ATMO_HEIGHT;
    return exp(-h * k) * (1.0 - h);
}

float optical_depth(vec3 o, vec3 d, float l, float f) {
    float sl = l / float(IN_SCATTER_SAMPLES);
    vec3 p = o + d * sl * 0.5;
    float r = 0.0;
    for (int i = 0; i < IN_SCATTER_SAMPLES; ++i) {
        r += density(p, f);
        p += d * sl;
    }
    return r * sl;
}

float phase_ray(float c) {
    return 3.0 / 4.0 * (1.0 - c * c);
}

vec3 in_scatter(vec3 o, vec3 d, float l) {
    float sl = l / float(IN_SCATTER_SAMPLES);
    vec3 p = o + d * sl * 0.5;
    float o0 = 0.0;
    vec3 s = vec3(0.0);
    for (int i = 0; i < IN_SCATTER_SAMPLES; ++i) {
        float a = density(p, F_RAY) * sl;
        o0 += a;
        float rl = sphere(p, SUN, CENTER, PLANET_RADIUS + ATMO_HEIGHT).y;
        float o = optical_depth(p, SUN, rl, F_RAY);
        vec3 t = exp(-(o0 + o) * K_RAY);
        s += t * sl;
        p += d * sl;
    }
    float c = dot(SUN, d);
    return s * phase_ray(c);
}

layout (binding = 0) uniform B {
	vec2 iResolution;
};

// [[requires(length(fwd) == 1.0)]]
vec3 ray_dir(vec3 fwd, vec2 uv, float fov) {
    float a = float(iResolution.x) / float(iResolution.y);
    float h = tan(radians(fov) * 0.5) * 2.0;;
    vec3 up = vec3(0, 1, 0);
    return normalize(fwd + h * up * uv.y + cross(up, fwd) * a * h * uv.x);
}

layout (location = 0) in vec2 frag_coord;
layout (location = 0) out vec4 frag_col;

void main() {
    vec2 uv = (frag_coord / iResolution.xy) * 2.0 - 1.0;
    vec3 o = vec3(0, 0, -3);
    vec3 d = ray_dir(vec3(0, 0, 1), uv, 60.0);
    vec2 ar = sphere(o, d, CENTER, PLANET_RADIUS + ATMO_HEIGHT);
    vec3 col = vec3(0.0);
    if (ar.x <= ar.y) {
        // col = vec3((ar.y - ar.x) / (2.0 * (PLANET_RADIUS + ATMO_HEIGHT)));
        col = in_scatter(o + d * ar.x, d, ar.y - ar.x);
    }
    frag_col = vec4(col, 1.0);
}
