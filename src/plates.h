// Tectonic plates: the first generation step. Plates are Voronoi cells on the
// sphere with warped edges; each has an Euler pole (so a velocity field) and
// is continental or oceanic. Where plates meet, their relative motion decides
// uplift (mountain belts), trenches, ridges and rifts. The result is a small
// per-cell table sampled by the height function on both CPU and GPU.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <random>
#include <vector>
#include <thread>

namespace plates {

constexpr int W = 1024, H = 512;   // ~40 km cells; boundaries are smooth
constexpr float PI_F = 3.14159265f;
constexpr float EARTH_RADIUS_KM = 6371.0f;

struct V3 {
    float x, y, z;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline V3 normalize(V3 a) {
    float l = std::sqrt(dot(a, a));
    return l > 0 ? a * (1.0f / l) : V3{0, 0, 1};
}

// One texel per cell, RGBA32F.
struct Cell {
    float uplift;    // -1 (trench/rift) .. 1 (collision belt)
    float crust;     // -1 oceanic .. 1 continental, smoothed across boundaries
    float cos2t;     // belt direction as a line angle from east, doubled so
    float sin2t;     //   it interpolates without a wrap at 180 degrees
};

struct Plate {
    V3 seed;
    V3 omega;        // Euler pole times rate (radians per unit time)
    bool continental;
};

struct Field {
    std::vector<Cell> cells; // W*H, row 0 = south
    std::vector<Plate> plates;

    // Bilinear sample at a unit vector, matching the GPU's GL_LINEAR lookup
    // with S repeating and T clamped.
    Cell sample(V3 n) const {
        float lat = std::asin(std::clamp(n.z, -1.0f, 1.0f));
        float lon = std::atan2(n.y, n.x);
        float u = (lon + PI_F) / (2 * PI_F) * W - 0.5f;
        float v = (lat + PI_F / 2) / PI_F * H - 0.5f;
        int x0 = (int)std::floor(u), y0 = (int)std::floor(v);
        float fx = u - x0, fy = v - y0;
        auto at = [&](int x, int y) -> const Cell& {
            x = ((x % W) + W) % W;
            y = std::clamp(y, 0, H - 1);
            return cells[y * W + x];
        };
        const Cell &a = at(x0, y0), &b = at(x0 + 1, y0), &c = at(x0, y0 + 1), &d = at(x0 + 1, y0 + 1);
        auto lerp = [](float p, float q, float t) { return p + (q - p) * t; };
        auto bil = [&](float ca, float cb, float cc, float cd) {
            return lerp(lerp(ca, cb, fx), lerp(cc, cd, fx), fy);
        };
        return {bil(a.uplift, b.uplift, c.uplift, d.uplift), bil(a.crust, b.crust, c.crust, d.crust),
                bil(a.cos2t, b.cos2t, c.cos2t, d.cos2t), bil(a.sin2t, b.sin2t, c.sin2t, d.sin2t)};
    }
};

inline V3 cellDir(int x, int y) {
    float lat = ((y + 0.5f) / H) * PI_F - PI_F / 2;
    float lon = ((x + 0.5f) / W) * 2 * PI_F - PI_F;
    return {std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat)};
}

// Small value-noise used only to warp plate edges and vary belt strength.
// Independent of the terrain noise so the two can be tuned separately.
inline float hashf(int x, int y, int z) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xffffff) / 16777215.0f * 2.0f - 1.0f;
}
inline float vnoise(V3 p) {
    int ix = (int)std::floor(p.x), iy = (int)std::floor(p.y), iz = (int)std::floor(p.z);
    float fx = p.x - ix, fy = p.y - iy, fz = p.z - iz;
    auto s = [](float t) { return t * t * (3 - 2 * t); };
    fx = s(fx); fy = s(fy); fz = s(fz);
    auto l = [](float a, float b, float t) { return a + (b - a) * t; };
    return l(l(l(hashf(ix, iy, iz), hashf(ix + 1, iy, iz), fx), l(hashf(ix, iy + 1, iz), hashf(ix + 1, iy + 1, iz), fx), fy),
             l(l(hashf(ix, iy, iz + 1), hashf(ix + 1, iy, iz + 1), fx), l(hashf(ix, iy + 1, iz + 1), hashf(ix + 1, iy + 1, iz + 1), fx), fy), fz);
}
inline float vfbm(V3 p, int oct) {
    float s = 0, a = 1, n = 0;
    for (int i = 0; i < oct; i++) {
        s += a * vnoise(p);
        n += a;
        a *= 0.5f;
        p = p * 2.1f + V3{7.3f, 1.9f, 4.7f};
    }
    return s / n;
}

inline Field build(uint32_t seed) {
    std::mt19937 rng(seed ^ 0x9E3779B9u);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    auto randDir = [&] {
        float z = u01(rng) * 2 - 1, a = u01(rng) * 2 * PI_F, r = std::sqrt(std::max(0.0f, 1 - z * z));
        return V3{r * std::cos(a), r * std::sin(a), z};
    };

    Field f;
    int count = 10 + (int)(u01(rng) * 8); // 10..17 plates
    for (int i = 0; i < count; i++) {
        Plate p;
        p.seed = randDir();
        p.omega = randDir() * (0.4f + 0.6f * u01(rng));
        p.continental = u01(rng) < 0.45f;
        f.plates.push_back(p);
    }
    V3 warpSeed = randDir() * 10.0f;

    // Warped position used for all plate-distance queries.
    auto warped = [&](V3 n) {
        V3 q = n + warpSeed;
        V3 warp = {vfbm(q * 2.0f, 4), vfbm(q * 2.0f + V3{31, 17, 5}, 4), vfbm(q * 2.0f + V3{9, 43, 23}, 4)};
        return normalize(n + warp * 0.22f);
    };

    auto bump = [](float s, float centre, float width) {
        float d = s - centre;
        return std::exp(-(d * d) / (width * width));
    };

    f.cells.resize(W * H);
    int threads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) {
        pool.emplace_back([&, t] {
            std::vector<float> dist(f.plates.size());
            for (int y = t; y < H; y += threads)
                for (int x = 0; x < W; x++) {
                    V3 n = cellDir(x, y);
                    V3 nw = warped(n);
                    int a = 0;
                    for (int i = 0; i < (int)f.plates.size(); i++) {
                        dist[i] = std::acos(std::clamp(dot(nw, f.plates[i].seed), -1.0f, 1.0f));
                        if (dist[i] < dist[a]) a = i;
                    }
                    const Plate& A = f.plates[a];

                    // Belt strength varies along boundaries so ranges have ends.
                    float along = 0.7f + 0.3f * vfbm(n * 4.0f + warpSeed, 3);

                    // Every other plate contributes a boundary profile weighted by
                    // its distance, so nothing depends on which plate is second
                    // nearest — that identity switches abruptly inside a plate and
                    // would otherwise draw a cliff along the switch line.
                    float uplift = 0, crustSum = A.continental ? 1.0f : -1.0f, crustW = 1;
                    float dirC = 0, dirS = 0;
                    for (int j = 0; j < (int)f.plates.size(); j++) {
                        if (j == a) continue;
                        const Plate& B = f.plates[j];
                        float distKm = (dist[j] - dist[a]) * 0.5f * EARTH_RADIUS_KM;
                        if (distKm > 1200.0f) continue;

                        float crustB = B.continental ? 1.0f : -1.0f;
                        float w = std::exp(-distKm / 200.0f);
                        crustSum += crustB * w;
                        crustW += w;

                        V3 toB = B.seed - A.seed;
                        V3 normal = normalize(toB - n * dot(toB, n));
                        V3 tangent = normalize(cross(n, normal));
                        V3 vA = cross(A.omega, n), vB = cross(B.omega, n);
                        float conv = dot(vA - vB, normal);

                        float u = 0;
                        if (conv > 0.05f) {
                            float c = std::min(conv / 0.35f, 1.0f) * along;
                            if (A.continental && B.continental) {          // collision: wide belt
                                u = c * bump(distKm, 0, 380);
                            } else if (A.continental != B.continental) {   // subduction: Andes-style
                                float sd = A.continental ? distKm : -distKm;
                                u = 0.9f * c * bump(sd, 160, 220) - 0.5f * c * bump(sd, -90, 90);
                            } else {                                       // island arc on the overriding plate
                                float sd = (a < j) ? distKm : -distKm;
                                u = 0.7f * c * bump(sd, 110, 110) - 0.4f * c * bump(sd, -70, 70);
                            }
                        } else if (conv < -0.05f) {
                            float c = std::min(-conv / 0.6f, 1.0f);
                            if (!A.continental) u = 0.22f * c * bump(distKm, 0, 220);   // mid-ocean ridge
                            else u = -0.18f * c * bump(distKm, 0, 90);                  // continental rift
                        }
                        uplift += u;

                        V3 east = normalize(V3{-n.y, n.x, 0});
                        V3 north = cross(n, east);
                        float theta = std::atan2(dot(tangent, north), dot(tangent, east));
                        float wd = std::fabs(u) + 1e-6f;
                        dirC += std::cos(2 * theta) * wd;
                        dirS += std::sin(2 * theta) * wd;
                    }
                    float dl = std::sqrt(dirC * dirC + dirS * dirS);
                    if (dl < 1e-9f) { dirC = 1; dirS = 0; dl = 1; }
                    f.cells[y * W + x] = {std::clamp(uplift, -1.0f, 1.0f), crustSum / crustW, dirC / dl, dirS / dl};
                }
        });
    }
    for (auto& th : pool) th.join();
    return f;
}

} // namespace plates
