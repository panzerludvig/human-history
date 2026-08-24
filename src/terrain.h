// CPU mirror of the terrain functions in shaders/globe.frag.
// Same hash, same noise, same constants, float arithmetic — so heights
// computed here match what the GPU draws (up to the octave count). Keep the
// two in sync: any change to a constant here must be made in the shader too.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <vector>
#include "plates.h"

namespace terrain {

// Unitless noise height to metres. Mountains top out around +8000 m.
constexpr float HEIGHT_SCALE_M = 8000.0f;
// How strongly continental vs oceanic crust shifts the continent field.
constexpr float CRUST_WEIGHT = 0.2f;

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

inline float ridged(V3 p, int octaves) {
    float sum = 0, amp = 0.5f, norm = 0;
    for (int i = 0; i < octaves; i++) {
        float n = 1.0f - std::fabs(noise(p));
        sum += amp * n * n;
        norm += amp;
        amp *= 0.55f;
        p = p * 2.07f + V3{3.3f, 9.1f, 21.7f};
    }
    return sum / norm;
}

// Hash of a lattice cell to three values in [0, 1).
inline V3 hash01(int ix, int iy, int iz) {
    uint32_t x = (uint32_t)ix, y = (uint32_t)iy, z = (uint32_t)iz;
    pcg3d(x, y, z);
    return {x / 4294967296.0f, y / 4294967296.0f, z / 4294967296.0f};
}

// Thrust blocks: Voronoi cells, each a tilted slab at its own random height.
// Cell edges are discontinuities — scarps where one block rides over the next.
// Rock heaps: every lattice cell owns a lopsided heap — height falling off
// from a random centre, tilted so one side is steep — and the terrain is the
// highest heap at each point. Where heaps meet, the crease is sharp and
// irregular: rock shoved against rock, with no uniform edge band.
// Feature points sit in the middle half of their cell, so the 2x2x2 cells
// around the point (found by rounding) hold every heap that can reach it.
inline float blocks(V3 p) {
    int cx = (int)std::floor(p.x - 0.5f), cy = (int)std::floor(p.y - 0.5f), cz = (int)std::floor(p.z - 0.5f);
    V3 f = {p.x - cx, p.y - cy, p.z - cz};
    float best = -1e9f;
    for (int dz = 0; dz <= 1; dz++)
        for (int dy = 0; dy <= 1; dy++)
            for (int dx = 0; dx <= 1; dx++) {
                int ix = cx + dx, iy = cy + dy, iz = cz + dz;
                V3 fp = hash01(ix, iy, iz) * 0.5f + 0.25f;
                V3 d = {f.x - (dx + fp.x), f.y - (dy + fp.y), f.z - (dz + fp.z)};
                V3 r = hash01(ix * 3 + 1, iy * 3 + 7, iz * 3 + 13);
                V3 t2 = hash01(ix + 5, iy - 3, iz + 9);
                V3 tilt = {r.y * 2 - 1, r.z * 2 - 1, t2.x * 2 - 1};
                // Faceted norm: three random axes make the heap a pyramid with
                // sharp edges rather than a round dome.
                V3 a1 = {1.0f, t2.y * 2 - 1, t2.z * 2 - 1};
                V3 a2 = {t2.z * 2 - 1, 1.0f, r.y * 2 - 1};
                V3 a3 = {r.z * 2 - 1, t2.x * 2 - 1, 1.0f};
                float dist = std::max({std::fabs(dot(d, a1)), std::fabs(dot(d, a2)), std::fabs(dot(d, a3))});
                float slope = 1.3f + t2.y * 0.9f;
                float h = r.x * 0.7f + 0.3f - dist * slope + dot(d, tilt) * 0.5f;
                best = std::max(best, h);
            }
    return best;
}

inline float fbm(V3 p, int octaves, float gain);

// Blocks at a given frequency with a domain warp so edges are crooked.
inline float warpedBlocks(V3 p, float freq, float seedOff) {
    V3 q = p * freq;
    V3 w = {fbm(q * 0.7f + seedOff, 2, 0.55f), fbm(q * 0.7f + seedOff + 7.0f, 2, 0.55f),
            fbm(q * 0.7f + seedOff + 19.0f, 2, 0.55f)};
    return blocks(q + w * 0.4f + seedOff);
}

inline float smoothstep(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

// The raw continent field before the sea level is subtracted.
inline float continentField(V3 p, const ContinentParams& cp) {
    V3 warp = {fbm(p * 1.3f + 11.0f, 3, 0.5f), fbm(p * 1.3f + 23.0f, 3, 0.5f), fbm(p * 1.3f + 37.0f, 3, 0.5f)};
    V3 q = p * cp.freq + warp * cp.warp;
    float base = fbm(q, 6, 0.5f);
    float web = 0.35f - std::fabs(fbm(q + 53.0f, 6, 0.5f)) * 2.0f;
    return base + (web - base) * cp.webness;
}

inline V3 rotate(const float rot[9], V3 v) {
    return {rot[0] * v.x + rot[3] * v.y + rot[6] * v.z,
            rot[1] * v.x + rot[4] * v.y + rot[7] * v.z,
            rot[2] * v.x + rot[5] * v.y + rot[8] * v.z};
}

// Height in metres above sea level. `p` is the point in noise space, `n` the
// unit surface normal in world space (the plate layer is indexed by it).
// `octaves` is the shader's level-of-detail value; the hydrology grid uses
// a fixed count matched to its cell size.
inline float heightMeters(V3 p, V3 n, const ContinentParams& cp, float seaLevel, int octaves,
                          const plates::Field& pf, const float rot[9]) {
    plates::Cell pl = pf.sample({n.x, n.y, n.z});
    float continent = continentField(p, cp) + pl.crust * CRUST_WEIGHT - seaLevel;
    float detail = fbm(p * 9.0f + 5.0f, std::max(octaves - 3, 1), 0.5f);

    // Mountain ranges: thrust blocks at three scales (sheets ~100 km,
    // blocks ~35 km, slabs ~12 km) with jagged ridged peaks on top. The
    // blocks are discontinuous at their edges by design: rock pushed over rock.
    V3 q = p * 7.0f + 2.0f;
    float peaks = ridged(q, std::max(octaves - 3, 1));
    float uplift = pl.uplift;
    // Blocks appear only where uplift is substantial; lowlands keep peaks/hills.
    float blockMask = smoothstep(0.35f, 0.75f, uplift);
    float stack = 0.5f;
    if (blockMask > 0.0f) {
        float sheets = warpedBlocks(p, 60.0f, 3.0f);
        float mid = octaves >= 7 ? warpedBlocks(p, 180.0f, 11.0f) : 0.5f;
        float slabs = octaves >= 10 ? warpedBlocks(p, 500.0f, 23.0f) : 0.5f;
        // Uplift only ever adds: heaps never cut below the belt's baseline.
        stack = std::max(sheets * 0.45f + mid * 0.3f + slabs * 0.25f, 0.05f);
    }
    // Below ~10 km the rock is ridges and gullies carved into the heap
    // faces: a ridged multifractal added on top, not more blocks.
    float gullies = octaves >= 11 ? ridged(p * 60.0f + 5.0f, std::max(octaves - 8, 1)) - 0.45f : 0.0f;
    float ranges = stack * blockMask * 0.7f * (0.55f + 0.45f * peaks) + peaks * 0.5f +
                   gullies * blockMask * 0.4f;
    float hills = ridged(p * 4.0f + 2.0f, std::clamp(octaves - 2, 1, 6)) *
                  smoothstep(0.02f, 0.25f, continent) *
                  smoothstep(0.3f, 0.7f, fbm(p * 2.2f + 41.0f, 3, 0.5f) * 0.5f + 0.5f);
    float h = continent + detail * 0.06f + ranges * std::max(uplift, 0.0f) * 0.9f +
              std::min(uplift, 0.0f) * 0.12f + hills * 0.12f;
    return h * HEIGHT_SCALE_M;
}

// Sea level such that `landFraction` of the sphere is above it, found by
// sampling the field over a Fibonacci sphere in the world's noise space.
inline float seaLevelFor(float landFraction, const ContinentParams& cp, const float rot[9], V3 offset,
                         const plates::Field& pf) {
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
        v[i] = continentField(w + offset, cp) + pf.sample({n.x, n.y, n.z}).crust * CRUST_WEIGHT;
    }
    float land = std::clamp(landFraction, 0.0f, 1.0f);
    int k = std::clamp((int)((1.0f - land) * N), 0, N - 1);
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

// ---------------------------------------------------------------- climate, substrate, vegetation
// Mirrors of the shader's classification (globe.frag) — keep in sync.

inline float temperatureC(float lat, float h) {
    return 28.0f - 45.0f * std::pow(std::fabs(lat) / 1.5707963f, 1.3f) - 6.5f * std::max(h, 0.0f) / 1000.0f;
}

inline float moistureAt(V3 w, float lat) {
    float m = fbm(w * 3.0f + 77.0f, 3, 0.5f) * 0.5f + 0.5f;
    float dryBand = std::exp(-std::pow((std::fabs(lat) - 0.42f) / 0.16f, 2.0f));
    return std::clamp(m - dryBand * 0.45f + 0.08f, 0.0f, 1.0f);
}

// Every point is a mixture: substrate fractions and cover fractions, each
// summing to 1. Mirrors the shader (globe.frag) — keep in sync.
constexpr int NSUB = 7;   // soil, sand, rock, scree, silt, mud, ice
constexpr int NCOV = 11;  // bare, tundra, taiga, forest, rainforest, grass, steppe, savanna, shrub, marsh, desert
enum Substrate { SUB_SOIL = 0, SUB_SAND, SUB_ROCK, SUB_SCREE, SUB_SILT, SUB_MUD, SUB_ICE };
enum Cover { COV_BARE = 0, COV_TUNDRA, COV_TAIGA, COV_FOREST, COV_RAINFOREST, COV_GRASS, COV_STEPPE,
             COV_SAVANNA, COV_SHRUB, COV_MARSH, COV_DESERT };

struct Mixture {
    float sub[NSUB];
    float cov[NCOV];
};

inline void pullTo(float* v, int n, int k, float t) {
    for (int i = 0; i < n; i++) v[i] *= 1.0f - t;
    v[k] += t;
}

inline float patchNoise(V3 w) { return fbm(w * 90.0f + 7.0f, 3, 0.5f) * 0.5f + 0.5f; }

// `swamp` (0..1) is the climate's waterlogging: it pulls flat ground toward
// mud, and marsh cover follows mud. 0 keeps the pre-climate behaviour.
// Fine within-region moisture variation, added to the coarse climate-derived
// moisture so vegetation keeps sub-cell texture. Mirrored in the shader.
inline float moistureDetail(V3 w) { return 0.12f * fbm(w * 5.0f + 31.0f, 3, 0.5f); }

inline Mixture mixtureAt(float h, float slope, float temp, float moist, float uplift, bool nearRiver, float patch,
                         float swamp = 0.0f) {
    Mixture m{};
    float* s = m.sub;
    s[0] = 1.0f;
    pullTo(s, NSUB, 1, smoothstep(0.3f, 0.18f, moist) * smoothstep(2.0f, 8.0f, temp));
    pullTo(s, NSUB, 4, nearRiver ? smoothstep(0.03f, 0.01f, slope) * smoothstep(900.0f, 600.0f, h) * 0.7f : 0.0f);
    pullTo(s, NSUB, 5, std::max(smoothstep(0.7f, 0.85f, moist) * smoothstep(0.025f, 0.01f, slope) * smoothstep(400.0f, 200.0f, h),
                                swamp * smoothstep(0.035f, 0.015f, slope)));
    pullTo(s, NSUB, 3, smoothstep(0.12f, 0.22f, slope) * smoothstep(0.2f, 0.4f, uplift));
    pullTo(s, NSUB, 2, std::max(smoothstep(0.28f, 0.4f, slope), smoothstep(3200.0f, 3900.0f, h)));
    pullTo(s, NSUB, 6, smoothstep(-11.0f, -16.0f, temp + slope * 4.0f));

    float* v = m.cov;
    float tree = smoothstep(0.36f, 0.62f, moist) * smoothstep(-3.0f, 4.0f, temp) * (0.45f + 0.55f * patch);
    float wT = smoothstep(9.0f, 3.0f, temp);
    float wR = smoothstep(18.0f, 23.0f, temp) * smoothstep(0.6f, 0.72f, moist);
    float wF = std::max(1.0f - wT - wR, 0.0f);
    float tn = wT + wR + wF;
    v[2] = tree * wT / tn; v[3] = tree * wF / tn; v[4] = tree * wR / tn;

    float open = 1.0f - tree;
    float oDesert = smoothstep(0.18f, 0.06f, moist);
    float oSteppe = smoothstep(0.1f, 0.22f, moist) * smoothstep(0.4f, 0.26f, moist);
    float oGrassy = smoothstep(0.26f, 0.38f, moist);
    float oSav = oGrassy * smoothstep(16.0f, 20.0f, temp) * smoothstep(0.6f, 0.45f, moist);
    float oGrass = oGrassy - oSav;
    float oShrub = smoothstep(0.1f, 0.2f, moist) * smoothstep(0.32f, 0.2f, moist) * (s[1] + 0.3f);
    float on = oDesert + oSteppe + oGrass + oSav + oShrub + 1e-5f;
    v[10] += open * oDesert / on; v[6] += open * oSteppe / on; v[5] += open * oGrass / on;
    v[7] += open * oSav / on; v[8] += open * oShrub / on;

    pullTo(v, NCOV, 9, s[5]);
    pullTo(v, NCOV, 1, smoothstep(1.0f, -5.0f, temp));
    float bare = std::max(std::max(s[2] + s[3] * 0.6f, s[6]), smoothstep(-8.0f, -15.0f, temp));
    bare = std::max(bare, smoothstep(0.1f, 0.03f, moist) * 0.5f);
    pullTo(v, NCOV, 0, std::clamp(bare, 0.0f, 1.0f));
    return m;
}

// Physical slope (rise over run) from two height samples ~500 m apart.
inline float slopeAt(V3 n, const ContinentParams& cp, float seaLevel, int octaves,
                     const plates::Field& pf, const float rot[9], V3 offset) {
    const float eps = 500.0f / 6371000.0f;
    V3 tx = std::fabs(n.z) < 0.99f ? V3{-n.y, n.x, 0} : V3{1, 0, 0};
    float tl = std::sqrt(dot(tx, tx));
    tx = tx * (1.0f / tl);
    V3 ty = {n.y * tx.z - n.z * tx.y, n.z * tx.x - n.x * tx.z, n.x * tx.y - n.y * tx.x};
    auto at = [&](V3 q) {
        float l = std::sqrt(dot(q, q));
        q = q * (1.0f / l);
        return heightMeters(rotate(rot, q) + offset, q, cp, seaLevel, octaves, pf, rot);
    };
    float h0 = at(n), hx = at(n + tx * eps), hy = at(n + ty * eps);
    return std::sqrt((hx - h0) * (hx - h0) + (hy - h0) * (hy - h0)) / 500.0f;
}

} // namespace terrain
