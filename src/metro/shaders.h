// GLSL sources for the scene and post-processing passes.
#pragma once

constexpr int MAX_LIGHTS = 96;

static const char* SCENE_VS = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in vec4 aParams;
uniform mat4 uViewProj;
out vec3 vPos;
out vec3 vNormal;
out vec3 vColor;
flat out vec4 vParams;
void main() {
    vPos = aPos;          // all geometry is built directly in world space
    vNormal = aNormal;
    vColor = aColor;
    vParams = aParams;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

static const char* SCENE_FS = R"(
#version 330 core
#define MAX_LIGHTS 96
in vec3 vPos;
in vec3 vNormal;
in vec3 vColor;
flat in vec4 vParams;
out vec4 FragColor;

layout(std140) uniform Lights {
    vec4 uPosR[MAX_LIGHTS];    // xyz = position, w = radius
    vec4 uColI[MAX_LIGHTS];    // rgb = color * intensity
    vec4 uDirCos[MAX_LIGHTS];  // xyz = spot direction, w = cos(outer angle) or -3 for point lights
    ivec4 uCount;
};

uniform vec3 uCamPos;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uAmbient;
uniform float uAlpha;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x),
               mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i) { s += a * noise(p); p *= 2.03; a *= 0.5; }
    return s;
}
// Pick 2D coordinates on the dominant plane of the surface.
vec2 planeUV(vec3 p, vec3 n) {
    vec3 a = abs(n);
    if (a.y >= a.x && a.y >= a.z) return p.xz;
    if (a.x >= a.z) return p.zy;
    return p.xy;
}

void applyPattern(int pat, vec3 p, vec3 n, inout vec3 albedo, inout float spec) {
    vec2 uv = planeUV(p, n);
    if (pat == 1) {            // floor tiles
        vec2 t = uv / 0.8;
        vec2 f = fract(t);
        float g = step(0.025, f.x) * step(0.025, f.y);
        albedo *= mix(0.45, 0.9 + 0.18 * hash12(floor(t)), g);
        albedo *= 0.88 + 0.12 * fbm(uv * 2.5);
        spec *= mix(0.2, 1.0, g);
    } else if (pat == 2) {     // wall tiles (running bond)
        vec2 t = uv / vec2(0.3, 0.15);
        t.x += 0.5 * mod(floor(t.y), 2.0);
        vec2 f = fract(t);
        float g = step(0.035, f.x) * step(0.07, f.y);
        albedo *= mix(0.55, 0.94 + 0.1 * hash12(floor(t)), g);
        spec *= mix(0.2, 1.0, g);
    } else if (pat == 3) {     // tactile dots
        vec2 f = fract(uv * 10.0) - 0.5;
        albedo *= length(f) < 0.28 ? 1.15 : 0.8;
    } else if (pat == 4) {     // ballast gravel
        float nn = noise(uv * 18.0) * 0.6 + noise(uv * 47.0) * 0.4;
        albedo *= 0.45 + 0.9 * nn;
        spec *= 0.2;
    } else if (pat == 5) {     // concrete
        albedo *= 0.78 + 0.32 * fbm(uv * 1.3);
        albedo *= 0.94 + 0.06 * noise(uv * 35.0);
    } else if (pat == 6) {     // escalator grooves
        albedo *= mix(0.45, 1.0, step(0.45, fract(p.z * 18.0)));
    } else if (pat == 7) {     // brushed metal
        albedo *= 0.9 + 0.12 * noise(vec2(uv.x * 3.0, uv.y * 140.0));
    } else if (pat == 9) {     // warning stripes
        albedo = mix(vec3(0.03), albedo, step(0.5, fract((uv.x + uv.y) * 2.0)));
    }
}

void main() {
    vec3 albedo = vColor;
    int pat = int(vParams.x + 0.5);
    float shin = vParams.y;
    float spec = vParams.z;
    float emis = vParams.w;

    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;
    applyPattern(pat, vPos, N, albedo, spec);

    vec3 V = normalize(uCamPos - vPos);
    float hemi = N.y * 0.5 + 0.5;
    vec3 color = albedo * mix(vec3(0.020, 0.018, 0.016), vec3(0.034, 0.037, 0.042), hemi) * uAmbient;

    for (int i = 0; i < uCount.x; ++i) {
        vec4 pr = uPosR[i];
        vec3 Lv = pr.xyz - vPos;
        float d2 = dot(Lv, Lv);
        if (d2 > pr.w * pr.w) continue;
        float d = sqrt(d2);
        vec3 L = Lv / d;
        float win = clamp(1.0 - pow(d / pr.w, 4.0), 0.0, 1.0);
        float att = win * win / (d2 + 1.0);
        vec4 dc = uDirCos[i];
        if (dc.w > -2.0) {
            float cd = dot(-L, dc.xyz);
            att *= smoothstep(dc.w, mix(dc.w, 1.0, 0.35), cd);
        }
        float ndl = max(dot(N, L), 0.0);
        if (ndl <= 0.0 || att <= 0.0) continue;
        vec3 H = normalize(L + V);
        float sp = pow(max(dot(N, H), 0.0), shin) * spec * (shin + 8.0) / 25.0;
        color += (albedo + sp) * ndl * uColI[i].rgb * att;
    }

    color += albedo * emis;

    float fres = pow(1.0 - max(dot(N, V), 0.0), 5.0);
    color += spec * fres * vec3(0.04, 0.045, 0.05);

    float dist = length(uCamPos - vPos);
    float fog = 1.0 - exp(-dist * uFogDensity);
    color = mix(color, uFogColor, fog * (emis > 0.0 ? 0.5 : 1.0));

    float alpha = uAlpha < 1.0 ? clamp(uAlpha + fres * 0.6, 0.0, 1.0) : 1.0;
    FragColor = vec4(color, alpha);
}
)";

// Fullscreen triangle generated from gl_VertexID.
static const char* FULLSCREEN_VS = R"(
#version 330 core
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

static const char* BRIGHT_FS = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTex;
uniform float uThreshold;
void main() {
    vec3 c = texture(uTex, vUV).rgb;
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float k = max(l - uThreshold, 0.0) / max(l, 1e-4);
    FragColor = vec4(min(c * k, vec3(40.0)), 1.0);
}
)";

static const char* BLUR_FS = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTex;
uniform vec2 uDir; // texel-sized step in blur direction
void main() {
    vec3 c = texture(uTex, vUV).rgb * 0.2270270270;
    c += texture(uTex, vUV + uDir * 1.3846153846).rgb * 0.3162162162;
    c += texture(uTex, vUV - uDir * 1.3846153846).rgb * 0.3162162162;
    c += texture(uTex, vUV + uDir * 3.2307692308).rgb * 0.0702702703;
    c += texture(uTex, vUV - uDir * 3.2307692308).rgb * 0.0702702703;
    FragColor = vec4(c, 1.0);
}
)";

static const char* COMPOSITE_FS = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uBloomStrength;
uniform float uExposure;
uniform vec2 uRes;
uniform int uCrosshair;
vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
void main() {
    vec3 c = texture(uScene, vUV).rgb + texture(uBloom, vUV).rgb * uBloomStrength;
    c = aces(c * uExposure);
    c = pow(c, vec3(1.0 / 2.2));
    float vig = smoothstep(1.05, 0.35, length(vUV - 0.5) * 1.35);
    c *= mix(0.55, 1.0, vig);
    if (uCrosshair != 0) {
        vec2 px = gl_FragCoord.xy - uRes * 0.5;
        float r = length(px);
        if (r < 2.5) c = mix(c, vec3(1.0), 0.85);
        else if (r < 3.5) c *= 0.4;
    }
    // Dither to hide banding in dark gradients
    float n = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    c += (n - 0.5) / 255.0;
    FragColor = vec4(c, 1.0);
}
)";
