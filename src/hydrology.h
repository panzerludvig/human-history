// Hydrology: lakes and rivers derived from the terrain function.
//
// The height function is sampled on an equirectangular grid, depressions are
// filled (priority flood) to find lake surfaces, and flow is accumulated
// downhill to find rivers. The result is a small per-cell table the shader
// reads as a texture; the function stays the source of truth, this is a
// derived layer that can be rebuilt from the seed at any time.
#pragma once
#include "terrain.h"
#include <queue>
#include <thread>
#include <cstring>

namespace hydrology {

constexpr int W = 2048, H = 1024;        // cells: ~20 km at the equator
constexpr float NO_LAKE = -1.0e6f;        // lakeLevel value meaning "no lake here"
constexpr float EARTH_RADIUS_KM = 6371.0f;
constexpr float PI_F = 3.14159265f;

// Neighbour offsets, index stored in the texture as the downstream direction.
constexpr int DX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
constexpr int DY[8] = {0, 1, 1, 1, 0, -1, -1, -1};

// One texel per cell, RGBA32F: lake level (m), drainage area (km^2),
// downstream direction (0..7, -1 none), terrain height (m).
struct Cell {
    float lakeLevel, flow, dir, height;
};

struct Result {
    std::vector<Cell> cells;   // W*H, row-major, row 0 = south
    int lakeCells = 0, riverCells = 0;
};

inline int wrapX(int x) { return (x + W) % W; }

inline terrain::V3 cellDir(int x, int y) {
    float lat = ((y + 0.5f) / H) * PI_F - PI_F / 2;
    float lon = ((x + 0.5f) / W) * 2 * PI_F - PI_F;
    return {std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat)};
}

inline float cellAreaKm2(int y) {
    float lat = ((y + 0.5f) / H) * PI_F - PI_F / 2;
    float dLat = PI_F / H, dLon = 2 * PI_F / W;
    return EARTH_RADIUS_KM * EARTH_RADIUS_KM * std::cos(lat) * dLat * dLon;
}

// Heights in metres for every cell, computed in parallel.
inline std::vector<float> sampleHeights(const terrain::ContinentParams& cp, float seaLevel,
                                        const float rot[9], terrain::V3 offset, const plates::Field& pf) {
    std::vector<float> h(W * H);
    const int octaves = 8; // finest detail ~ cell size
    int threads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) {
        pool.emplace_back([&, t] {
            for (int y = t; y < H; y += threads)
                for (int x = 0; x < W; x++) {
                    terrain::V3 n = cellDir(x, y);
                    terrain::V3 w = {rot[0] * n.x + rot[3] * n.y + rot[6] * n.z,
                                     rot[1] * n.x + rot[4] * n.y + rot[7] * n.z,
                                     rot[2] * n.x + rot[5] * n.y + rot[8] * n.z};
                    h[y * W + x] = terrain::heightMeters(w + offset, n, cp, seaLevel, octaves, pf, rot);
                }
        });
    }
    for (auto& th : pool) th.join();
    return h;
}

inline Result build(const terrain::ContinentParams& cp, float seaLevel, const float rot[9],
                    terrain::V3 offset, float riverThresholdKm2, const plates::Field& pf) {
    std::vector<float> h = sampleHeights(cp, seaLevel, rot, offset, pf);
    const int N = W * H;
    std::vector<float> filled(N);
    std::vector<int> parent(N, -1);
    std::vector<char> seen(N, 0);
    std::vector<int> order;
    order.reserve(N);

    // Priority flood, seeded from the ocean. Each cell's water surface is the
    // lowest it can drain to; where that is above the ground, it is a lake.
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    for (int i = 0; i < N; i++)
        if (h[i] <= 0) {
            filled[i] = h[i];
            seen[i] = 1;
            pq.push({h[i], i});
        }
    if (pq.empty()) { // no ocean at all: drain to the lowest point
        int lo = (int)(std::min_element(h.begin(), h.end()) - h.begin());
        filled[lo] = h[lo];
        seen[lo] = 1;
        pq.push({h[lo], lo});
    }
    while (!pq.empty()) {
        auto [lvl, c] = pq.top();
        pq.pop();
        order.push_back(c);
        int cx = c % W, cy = c / W;
        for (int d = 0; d < 8; d++) {
            int nx = wrapX(cx + DX[d]), ny = cy + DY[d];
            if (ny < 0 || ny >= H) continue;
            int n = ny * W + nx;
            if (seen[n]) continue;
            seen[n] = 1;
            filled[n] = std::max(h[n], filled[c]);
            parent[n] = c;
            pq.push({filled[n], n});
        }
    }

    // Flow accumulation: children are popped after parents, so walking the
    // pop order backwards completes each cell before its parent sums it.
    std::vector<float> acc(N);
    for (int i = 0; i < N; i++) acc[i] = cellAreaKm2(i / W);
    for (int k = N - 1; k >= 0; k--) {
        int c = order[k];
        if (parent[c] >= 0) acc[parent[c]] += acc[c];
    }

    // Full filling makes every broad basin a sea. Real basins are mostly
    // breached by erosion, so cap each lake's depth above its floor; a few
    // basins keep their full fill and become great lakes.
    std::vector<float> level(filled);
    {
        std::vector<int> comp(N, -1);
        std::vector<int> stack;
        int ncomp = 0;
        for (int i = 0; i < N; i++) {
            bool lake = h[i] > 0 && filled[i] > h[i] + 0.5f;
            if (!lake || comp[i] >= 0) continue;
            float floorH = h[i];
            std::vector<int> members;
            stack.push_back(i);
            comp[i] = ncomp;
            while (!stack.empty()) {
                int c = stack.back();
                stack.pop_back();
                members.push_back(c);
                floorH = std::min(floorH, h[c]);
                int cx = c % W, cy = c / W;
                for (int d = 0; d < 8; d++) {
                    int nx = wrapX(cx + DX[d]), ny = cy + DY[d];
                    if (ny < 0 || ny >= H) continue;
                    int n = ny * W + nx;
                    bool nlake = h[n] > 0 && filled[n] > h[n] + 0.5f;
                    if (!nlake || comp[n] >= 0 || filled[n] != filled[i]) continue;
                    comp[n] = ncomp;
                    stack.push_back(n);
                }
            }
            uint32_t hx = (uint32_t)i * 2654435761u;
            hx ^= hx >> 15;
            // Most basins are breached and hold no lake at all; of the rest,
            // a few keep their full fill and become great lakes.
            int roll = (int)(hx % 100);
            bool great = roll < 3;
            bool kept = roll < 30;
            float cap = great ? 1.0e9f : 60.0f;
            float lvl = kept ? std::min(filled[i], floorH + cap) : floorH;
            for (int c : members) level[c] = lvl;
            ncomp++;
        }
    }

    Result r;
    r.cells.resize(N);
    for (int i = 0; i < N; i++) {
        Cell& cell = r.cells[i];
        bool ocean = h[i] <= 0;
        bool lake = !ocean && level[i] > h[i] + 0.5f;
        cell.height = h[i];
        cell.lakeLevel = lake ? level[i] : NO_LAKE;
        // Rivers are drawn from land cells only; lakes and the sea absorb them.
        cell.flow = (!ocean && !lake && acc[i] >= riverThresholdKm2) ? acc[i] : 0.0f;
        cell.dir = -1;
        if (parent[i] >= 0) {
            int px = parent[i] % W, py = parent[i] / W, cx = i % W, cy = i / W;
            for (int d = 0; d < 8; d++)
                if (wrapX(cx + DX[d]) == px && cy + DY[d] == py) cell.dir = (float)d;
        }
        if (lake) r.lakeCells++;
        if (cell.flow > 0) r.riverCells++;
    }

    // Let lake levels spill one cell outward so the shoreline is decided by
    // the GPU's height rather than the grid's cell edges.
    std::vector<float> spill(N, NO_LAKE);
    for (int i = 0; i < N; i++) {
        if (r.cells[i].lakeLevel != NO_LAKE) continue;
        int cx = i % W, cy = i / W;
        float best = NO_LAKE;
        for (int d = 0; d < 8; d++) {
            int nx = wrapX(cx + DX[d]), ny = cy + DY[d];
            if (ny < 0 || ny >= H) continue;
            best = std::max(best, r.cells[ny * W + nx].lakeLevel);
        }
        spill[i] = best;
    }
    for (int i = 0; i < N; i++)
        if (spill[i] != NO_LAKE && h[i] > 0) r.cells[i].lakeLevel = spill[i];
    return r;
}

} // namespace hydrology
