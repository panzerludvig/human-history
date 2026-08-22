// CPU mirror of the continent field in shaders/globe.frag.
// Same hash, same noise, same constants, float arithmetic — so a threshold
// chosen here lands on the same coastlines the GPU draws. Keep the two in sync.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <vector>

namespace terrain {

struct V3 {
    float x, y, z;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline V3 operator+(V3 a, float s) { return {a.x + s, a.y + s, a.z + s}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Parameters that shape continents; mirrored as uniforms in the shader.
struct ContinentParams {
    float freq;      // base frequency of the continent field
    float warp;      // domain-warp strength
    float webness;   // 0 = blobs, 1 = ridge web of thin strips
};

// concentration in [0, 1]: 1 = one massive continent, 0 = island webs.
inline ContinentParams paramsFor(float concentration) {
    float c = std::clamp(concentration, 0.0f, 1.0f);
    ContinentParams p;
    p.freq = 0.55f + 2.2f * (1.0f - c);
    p.warp = 0.25f + 0.55f * (1.0f - c);
    float t = std::clamp(1.0f - c / 0.45f, 0.0f, 1.0f);
    p.webness = t * t * (3.0f - 2.0f * t); // smoothstep(0.45, 0, c)
    return p;
}

inline void pcg3d(uint32_t& x, uint32_t& y, uint32_t& z) {
    x = x * 1664525u + 1013904223u;
    y = y * 1664525u + 1013904223u;
    z = z * 1664525u + 1013904223u;
    x += y * z; y += z * x; z += x * y;
    x ^= x >> 16; y ^= y >> 16; z ^= z >> 16;
    x += y * z; y += z * x; z += x * y;
}

inline V3 gradient(int ix, int iy, int iz) {
    uint32_t x = (uint32_t)ix, y = (uint32_t)iy, z = (uint32_t)iz;
    pcg3d(x, y, z);
    V3 g = {x / 4294967295.0f * 2.0f - 1.0f, y / 4294967295.0f * 2.0f - 1.0f, z / 4294967295.0f * 2.0f - 1.0f};
    float l = std::sqrt(dot(g, g));
    return g * (1.0f / l);
}

inline float noise(V3 p) {
    int ix = (int)std::floor(p.x), iy = (int)std::floor(p.y), iz = (int)std::floor(p.z);
    V3 f = {p.x - ix, p.y - iy, p.z - iz};
    V3 u = {f.x * f.x * (3 - 2 * f.x), f.y * f.y * (3 - 2 * f.y), f.z * f.z * (3 - 2 * f.z)};
    auto corner = [&](int dx, int dy, int dz) {
        return dot(gradient(ix + dx, iy + dy, iz + dz), f - V3{(float)dx, (float)dy, (float)dz});
    };
    auto mix = [](float a, float b, float t) { return a + (b - a) * t; };
    float n000 = corner(0, 0, 0), n100 = corner(1, 0, 0), n010 = corner(0, 1, 0), n110 = corner(1, 1, 0);
    float n001 = corner(0, 0, 1), n101 = corner(1, 0, 1), n011 = corner(0, 1, 1), n111 = corner(1, 1, 1);
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z) * 1.6f;
}

inline float fbm(V3 p, int octaves, float gain) {
    float sum = 0, amp = 1, norm = 0;
    for (int i = 0; i < octaves; i++) {
        sum += amp * noise(p);
        norm += amp;
        amp *= gain;
        p = p * 2.03f + V3{17.1f, 31.7f, 5.3f};
    }
    return sum / norm;
}

// The raw continent field before the sea level is subtracted.
inline float continentField(V3 p, const ContinentParams& cp) {
    V3 warp = {fbm(p * 1.3f + 11.0f, 3, 0.5f), fbm(p * 1.3f + 23.0f, 3, 0.5f), fbm(p * 1.3f + 37.0f, 3, 0.5f)};
    V3 q = p * cp.freq + warp * cp.warp;
    float base = fbm(q, 6, 0.5f);
    float web = 0.35f - std::fabs(fbm(q + 53.0f, 6, 0.5f)) * 2.0f;
    return base + (web - base) * cp.webness;
}

// Sea level such that `landFraction` of the sphere is above it, found by
// sampling the field over a Fibonacci sphere in the world's noise space.
inline float seaLevelFor(float landFraction, const ContinentParams& cp, const float rot[9], V3 offset) {
    const int N = 40000;
    std::vector<float> v(N);
    const float golden = 2.39996323f;
    for (int i = 0; i < N; i++) {
        float y = 1.0f - 2.0f * (i + 0.5f) / N;
        float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
        float a = golden * i;
        V3 n = {r * std::cos(a), y, r * std::sin(a)};
        // Column-major mat3 times n, plus offset — same as the shader.
        V3 w = {rot[0] * n.x + rot[3] * n.y + rot[6] * n.z,
                rot[1] * n.x + rot[4] * n.y + rot[7] * n.z,
                rot[2] * n.x + rot[5] * n.y + rot[8] * n.z};
        v[i] = continentField(w + offset, cp);
    }
    float land = std::clamp(landFraction, 0.0f, 1.0f);
    int k = std::clamp((int)((1.0f - land) * N), 0, N - 1);
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

} // namespace terrain
