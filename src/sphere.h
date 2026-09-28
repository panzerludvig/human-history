// Positions on the globe as the simulation uses them: a cell's centre, the
// cell under a point, the distance between two points, a step along a
// great circle. The lat-lon grid shows through only in cellCentre and
// cellOf (Technical/Geodesic Grid.md is the grid that will replace it);
// everything above them works on unit vectors.
#pragma once
#include "settlement.h"
#include <cmath>

namespace sim {

// The centre of a grid cell. Mirrored in globe.frag cellCentre.
inline terrain::V3 cellCentre(int cell) {
    hydrology::V3orig d = hydrology::cellDir(cell % population::W, cell / population::W);
    return {d.x, d.y, d.z};
}

inline int cellOf(terrain::V3 n) {
    float lat = std::asin(std::clamp(n.z, -1.0f, 1.0f));
    float lon = std::atan2(n.y, n.x);
    int cx =
        hydrology::wrapX((int)std::floor((lon + 3.14159265f) / (2 * 3.14159265f) * population::W));
    int cy = std::clamp((int)std::floor((lat + 3.14159265f / 2) / 3.14159265f * population::H), 0,
                        population::H - 1);
    return cy * population::W + cx;
}

// Haversine-style: acos(dot) loses about 3 km of precision on nearly equal
// unit vectors, which is fatal for the kilometre-scale tests (marker picking,
// arrival checks). The chord form stays exact all the way down to zero.
inline float distKm(terrain::V3 a, terrain::V3 b) {
    terrain::V3 d{a.x - b.x, a.y - b.y, a.z - b.z};
    float half = std::sqrt(terrain::dot(d, d)) * 0.5f;
    return 2.0f * std::asin(std::clamp(half, 0.0f, 1.0f)) * 6371.0f;
}

inline terrain::V3 norm3(terrain::V3 v) {
    float l = std::sqrt(terrain::dot(v, v));
    return {v.x / l, v.y / l, v.z / l};
}

// Great-circle step of `km` from p toward q.
inline terrain::V3 moveToward(terrain::V3 p, terrain::V3 q, float km) {
    float ang = std::acos(std::clamp(terrain::dot(p, q), -1.0f, 1.0f));
    float step = km / 6371.0f;
    if (ang <= step || ang < 1e-6f) return q;
    float t = step / ang, sa = std::sin(ang);
    return norm3(p * (std::sin((1 - t) * ang) / sa) + q * (std::sin(t * ang) / sa));
}

} // namespace sim
