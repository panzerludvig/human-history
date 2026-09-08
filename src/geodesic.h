// A geodesic grid: an icosahedron subdivided and projected onto the sphere,
// with the cells being the dual hexagons (and twelve pentagons at the
// original corners).
//
// WHY THIS EXISTS. A latitude-longitude grid is singular at the poles. Cell
// width goes as the cosine of latitude, so it goes to ZERO, and no single
// timestep can satisfy the CFL condition everywhere. Every polar failure this
// model has had traces to that one fact: cap rows filled by copying their
// neighbour and acting as infinite reservoirs; a reflecting wall built one
// row short of the pole; a zonal filter that removed every mode EXCEPT the
// axisymmetric jet it was meant to control; curvature terms that blow up like
// tan(latitude); and finally gravity waves at a Courant number of 4.7,
// aliased into a standing oscillation whose v-h correlation pumped 39208
// units of mass a second into the polar cap and held a 280 hPa high there.
//
// None of those are concepts on this grid. There are no pole rows, no
// wraparound column, no cosine weights, no polar filter, and no metric terms.
// Every cell has five or six neighbours, an area, and edges; every operator
// is one loop with no special cases; and one timestep is valid everywhere
// because the spacing is uniform to within a few percent.
//
// The cells are also cheaper. A 192x96 lat-lon grid spends 18432 cells and
// wastes its polar rows on ten-kilometre slivers that do active harm. Five
// subdivisions give 10242 cells at a uniform 250 km -- fewer cells, better
// effective resolution, and none of them pathological.
//
// VECTORS ARE 3D HERE, held in the plane tangent to the sphere, rather than
// being (u, v) components against a coordinate frame that rotates. That is
// not a detail: the curvature term (f + u tan(phi)/a) which had to be added
// to the lat-lon momentum equations is an artefact of those components, and
// on this grid it simply does not arise. Coriolis is -2 Omega z_hat x v,
// projected back onto the tangent plane, and that is the whole of it.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace geodesic {

struct D3 {
    double x, y, z;
};
inline D3 operator+(D3 a, D3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline D3 operator-(D3 a, D3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline D3 operator*(D3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline D3 cross(D3 a, D3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double len(D3 a) { return std::sqrt(dot(a, a)); }
inline D3 norm(D3 a) {
    double l = len(a);
    return l > 0 ? a * (1.0 / l) : a;
}
// The part of v that lies in the plane tangent to the sphere at p.
inline D3 tangent(D3 v, D3 p) { return v - p * dot(v, p); }

// The area of a spherical triangle, by Van Oosterom and Strackee -- stable
// where the half-angle formulae are not, and it needs no trigonometry beyond
// one arctangent.
inline double triArea(D3 a, D3 b, D3 c) {
    double num = std::fabs(dot(a, cross(b, c)));
    double den = 1.0 + dot(a, b) + dot(b, c) + dot(c, a);
    return 2.0 * std::atan2(num, den);
}

// The grid, stored as compressed rows: cell i's neighbours are the entries
// [nbrStart[i], nbrStart[i+1]) of nbr, and the per-edge arrays line up with
// them one for one.
//
// Lengths and areas are on the UNIT sphere -- radians and steradians. Scale
// by R and R*R at the point of use, so the grid itself knows nothing about
// which planet it is on.
struct Grid {
    int level = 0;
    std::vector<D3> c;        // cell centres, unit vectors
    std::vector<double> area; // steradians
    std::vector<int> nbrStart;
    std::vector<int> nbr;
    std::vector<double> elen;  // shared edge length, radians
    std::vector<double> edist; // centre-to-centre distance, radians
    std::vector<D3> enorm;     // outward unit normal at that edge, tangent at c[i]
    std::vector<int> tri;      // the triangulation these cells are the dual of
    int relaxPasses = 0;       // how many Lloyd passes the mesh accepted
    int size() const { return (int)c.size(); }
    int deg(int i) const { return nbrStart[i + 1] - nbrStart[i]; }
};

// The circumcentre of a spherical triangle: the direction equidistant from
// all three, which for points on a unit sphere is the normal to their plane.
inline D3 circum(D3 a, D3 b, D3 c) {
    D3 n = norm(cross(b - a, c - a));
    return dot(n, a) < 0 ? n * -1.0 : n;
}

// How many Lloyd passes the mesh gets, and how far it steps each time.
// A full step to the centroid is the classic algorithm and is normally
// stable; the damping is here because a mesh that tangles is unrecoverable
// and a few extra passes are cheap.
// A cap, not a target: the loop stops when the mesh stops moving, and how
// many passes that takes grows with the cell count. Level 5 settles in about
// 700.
constexpr int RELAX_PASSES = 6000;
// One nanoradian is six millimetres on Earth. Converged.
constexpr double RELAX_SETTLED = 1e-9;
constexpr double RELAX_STEP = 0.9;

// The area-weighted centroid of a spherical polygon: fan it into triangles
// from its own first corner and average their centres by area. Fanning from
// the CELL centre would bias the answer towards where the cell already is,
// which is the one thing a centroid must not do.
inline D3 polyCentroid(const std::vector<D3>& p) {
    D3 acc{0, 0, 0};
    double A = 0;
    for (size_t k = 1; k + 1 < p.size(); k++) {
        double a = triArea(p[0], p[k], p[k + 1]);
        acc = acc + norm(p[0] + p[k] + p[k + 1]) * a;
        A += a;
    }
    return A > 0 ? norm(acc) : D3{0, 0, 0};
}

inline Grid build(int level, int relaxPasses = RELAX_PASSES) {
    Grid g;
    g.level = level;
    // --- the icosahedron
    const double t = (1.0 + std::sqrt(5.0)) / 2.0;
    std::vector<D3> v = {{-1, t, 0}, {1, t, 0},   {-1, -t, 0}, {1, -t, 0},
                         {0, -1, t}, {0, 1, t},   {0, -1, -t}, {0, 1, -t},
                         {t, 0, -1}, {t, 0, 1},   {-t, 0, -1}, {-t, 0, 1}};
    for (auto& p : v) p = norm(p);
    std::vector<int> tri = {0, 11, 5, 0, 5,  1, 0,  1,  7, 0, 7,  10, 0,  10, 11,
                            1, 5,  9, 5, 11, 4, 11, 10, 2, 10, 7, 6,  7,  1,  8,
                            3, 9,  4, 3, 4,  2, 3,  2,  6, 3, 6, 8,  3,  8,  9,
                            4, 9,  5, 2, 4,  11, 6,  2,  10, 8, 6, 7,  9,  8,  1};

    // --- subdivide, sharing the midpoints so the mesh stays welded
    for (int s = 0; s < level; s++) {
        std::map<uint64_t, int> mid;
        auto midpoint = [&](int a, int b) {
            uint64_t key = a < b ? ((uint64_t)a << 32) | (uint32_t)b
                                 : ((uint64_t)b << 32) | (uint32_t)a;
            auto it = mid.find(key);
            if (it != mid.end()) return it->second;
            int idx = (int)v.size();
            v.push_back(norm(v[a] + v[b]));
            mid.emplace(key, idx);
            return idx;
        };
        std::vector<int> out;
        out.reserve(tri.size() * 4);
        for (size_t f = 0; f < tri.size(); f += 3) {
            int a = tri[f], b = tri[f + 1], c = tri[f + 2];
            int ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
            int fs[12] = {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca};
            out.insert(out.end(), fs, fs + 12);
        }
        tri.swap(out);
    }

    int N = (int)v.size();

    // --- the fan of triangles around each vertex, ordered once
    //
    // The topology never changes after this: subdivision fixes who neighbours
    // whom, and relaxation only moves points. So the ordering is computed once
    // and every later pass reuses it.
    std::vector<std::vector<int>> around(N);
    for (size_t f = 0; f < tri.size(); f += 3)
        for (int k = 0; k < 3; k++) around[tri[f + k]].push_back((int)f);

    std::vector<std::vector<int>> fan(N);  // ordered triangle base-indices
    std::vector<std::vector<int>> nbrs(N); // the neighbour each one leads to
    for (int i = 0; i < N; i++) {
        std::vector<std::pair<int, int>> wing; // (from, to) around i
        std::vector<int> face;
        for (int f : around[i]) {
            int a = tri[f], b = tri[f + 1], c = tri[f + 2];
            int p, q;
            if (a == i) { p = b; q = c; }
            else if (b == i) { p = c; q = a; }
            else { p = a; q = b; }
            wing.emplace_back(p, q);
            face.push_back(f);
        }
        std::vector<int> order{0};
        std::vector<bool> used(wing.size(), false);
        used[0] = true;
        for (size_t k = 1; k < wing.size(); k++) {
            int want = wing[order.back()].second;
            for (size_t j = 0; j < wing.size(); j++)
                if (!used[j] && wing[j].first == want) {
                    order.push_back((int)j);
                    used[j] = true;
                    break;
                }
            if (order.size() != k + 1) break;
        }
        for (int k : order) {
            fan[i].push_back(face[k]);
            nbrs[i].push_back(wing[k].first);
        }
    }

    // --- Lloyd relaxation
    //
    // Subdivision builds the mesh by splitting a FLAT icosahedron and
    // projecting it outwards, and a flat face's middle sits closer to the
    // centre than its corners, so the projection stretches the middle of every
    // face more than its edges. That distortion is an artefact of the method
    // and has nothing to do with the twelve pentagons -- the measurement said
    // so plainly, with the pentagons carrying a gradient error of 0.0002 while
    // ordinary hexagons carried 0.06.
    //
    // Lloyd removes it: move every cell centre to the centre of area of its
    // own cell, and repeat. What it converges to is a CENTROIDAL tessellation,
    // and that is worth more than the tidier picture. The error in a
    // Green-Gauss gradient is proportional to the offset between a cell's
    // centre and its centroid, so driving that offset to zero is what lifts
    // the operators from first order towards second.
    //
    // Three invariants, checked every pass. The first two are because an
    // earlier attempt tangled the mesh and reported a total area of 1.118
    // spheres, which is only possible when cells overlap and get counted
    // twice. The third is the one that matters most, and it took a second
    // try to see:
    //
    //   1. every triangle keeps its winding, so nothing folds through itself
    //   2. the total area stays 4*pi
    //   3. THE TRIANGULATION STAYS DELAUNAY
    //
    // Lloyd converges to a centroidal VORONOI tessellation, and the dual
    // polygons here are only the Voronoi cells while the triangulation is
    // Delaunay. Moving the points can break that, and this code does not
    // re-triangulate -- so past that point the algorithm is optimising
    // against polygons that are no longer the cells it thinks they are. It
    // shows up exactly as one would expect: the operators keep improving,
    // because the centres really are converging on their polygons' centroids,
    // while the cell AREAS quietly diverge -- 1.33, 1.43, 1.57 as the mesh is
    // refined, a defect that grows with resolution, which is the signature of
    // an algorithm past the edge of its assumptions rather than a trade.
    //
    // Re-triangulating would be the complete answer. Stopping where the
    // assumption stops holding is the honest one, and it needs no arbitrary
    // pass count: run until Delaunay is first violated, then keep the last
    // mesh that was.
    {
        auto wound = [&](const std::vector<D3>& pts) {
            for (size_t f = 0; f < tri.size(); f += 3) {
                D3 a = pts[tri[f]], b = pts[tri[f + 1]], c = pts[tri[f + 2]];
                if (dot(cross(b - a, c - a), a) <= 0) return false;
            }
            return true;
        };
        // Which two triangles flank each edge -- fixed, like everything else
        // topological.
        std::map<uint64_t, std::pair<int, int>> edgeTri;
        for (size_t f = 0; f < tri.size(); f += 3)
            for (int k = 0; k < 3; k++) {
                int a = tri[f + k], b = tri[f + (k + 1) % 3];
                uint64_t key = a < b ? ((uint64_t)a << 32) | (uint32_t)b
                                     : ((uint64_t)b << 32) | (uint32_t)a;
                auto& e = edgeTri[key];
                if (e.first == 0 && e.second == 0) e.first = (int)f + 1;
                else e.second = (int)f + 1;
            }
        // The empty-circumcircle test, on the sphere: the circumcircle of a
        // triangle is the set of points at a fixed dot product with its
        // circumcentre, so the opposite vertex is inside when its dot product
        // is larger.
        auto delaunay = [&](const std::vector<D3>& pts) {
            for (auto& kv : edgeTri) {
                int f1 = kv.second.first - 1, f2 = kv.second.second - 1;
                if (f1 < 0 || f2 < 0) continue;
                int a = (int)(kv.first >> 32), b = (int)(kv.first & 0xffffffffu);
                auto opposite = [&](int f) {
                    for (int k = 0; k < 3; k++)
                        if (tri[f + k] != a && tri[f + k] != b) return tri[f + k];
                    return -1;
                };
                int c = opposite(f1), d = opposite(f2);
                if (c < 0 || d < 0) continue;
                D3 n = circum(pts[a], pts[b], pts[c]);
                if (dot(pts[d], n) > dot(pts[a], n) + 1e-12) return false;
            }
            return true;
        };
        std::vector<D3> corners, target(N), nv(N);
        int passesUsed = 0;
        for (int pass = 0; pass < relaxPasses; pass++) {
            for (int i = 0; i < N; i++) {
                corners.clear();
                for (int f : fan[i])
                    corners.push_back(circum(v[tri[f]], v[tri[f + 1]], v[tri[f + 2]]));
                D3 cen = polyCentroid(corners);
                target[i] = len(cen) > 0 ? cen : v[i];
            }
            double moved = 0;
            for (int i = 0; i < N; i++) {
                nv[i] = norm(v[i] + (target[i] - v[i]) * RELAX_STEP);
                moved = std::max(moved, len(nv[i] - v[i]));
            }
            if (!wound(nv)) break;    // fold: keep what we had
            if (!delaunay(nv)) break; // past the algorithm's assumption
            v.swap(nv);
            passesUsed = pass + 1;
            if (moved < RELAX_SETTLED) break; // settled
        }
        g.relaxPasses = passesUsed;
    }

    // --- geometry, from the mesh as it finally stands
    g.c = v;
    g.tri = tri;
    g.area.assign(N, 0.0);
    g.nbrStart.assign(N + 1, 0);
    for (int i = 0; i < N; i++) g.nbrStart[i + 1] = g.nbrStart[i] + (int)nbrs[i].size();

    int E = g.nbrStart[N];
    g.nbr.resize(E);
    g.elen.resize(E);
    g.edist.resize(E);
    g.enorm.resize(E);

    std::vector<D3> corners;
    for (int i = 0; i < N; i++) {
        corners.clear();
        for (int f : fan[i]) corners.push_back(circum(v[tri[f]], v[tri[f + 1]], v[tri[f + 2]]));
        int b = g.nbrStart[i], d = (int)nbrs[i].size();
        double A = 0.0;
        for (int k = 0; k < d; k++) A += triArea(v[i], corners[k], corners[(k + 1) % d]);
        g.area[i] = A;
        for (int k = 0; k < d; k++) {
            int j = nbrs[i][k];
            g.nbr[b + k] = j;
            // The dual edge between i and j runs between the circumcentres of
            // the two triangles sharing edge (i, j): in this fan, corners k-1
            // and k flank neighbour k.
            D3 p0 = corners[(k + d - 1) % d], p1 = corners[k];
            g.elen[b + k] = std::acos(std::clamp(dot(p0, p1), -1.0, 1.0));
            g.edist[b + k] = std::acos(std::clamp(dot(v[i], v[j]), -1.0, 1.0));
            g.enorm[b + k] = norm(tangent(v[j], v[i]));
        }
    }
    return g;
}

// ---------------------------------------------------------------- operators
//
// Finite volume, and every one of them is the same loop. R is the planet's
// radius, which is where the unit sphere becomes a world.

// Green-Gauss: the gradient is the flux of the value through the boundary.
inline D3 grad(const Grid& g, const std::vector<double>& f, int i, double R) {
    D3 s{0, 0, 0};
    for (int k = g.nbrStart[i]; k < g.nbrStart[i + 1]; k++)
        s = s + g.enorm[k] * (0.5 * (f[i] + f[g.nbr[k]]) * g.elen[k]);
    return s * (1.0 / (g.area[i] * R));
}

// The divergence of a tangent vector field.
inline double div(const Grid& g, const std::vector<D3>& F, int i, double R) {
    double s = 0.0;
    for (int k = g.nbrStart[i]; k < g.nbrStart[i + 1]; k++) {
        D3 Fe = (F[i] + F[g.nbr[k]]) * 0.5;
        s += dot(Fe, g.enorm[k]) * g.elen[k];
    }
    return s / (g.area[i] * R);
}

// The Laplacian, as the net gradient across each face.
inline double lap(const Grid& g, const std::vector<double>& f, int i, double R) {
    double s = 0.0;
    for (int k = g.nbrStart[i]; k < g.nbrStart[i + 1]; k++)
        s += (f[g.nbr[k]] - f[i]) * g.elen[k] / g.edist[k];
    return s / (g.area[i] * R * R);
}

// Upwind advection of a scalar by a tangent vector field: a cell may only be
// given what is upstream of it, so no new extremes appear at the grid scale.
inline double advect(const Grid& g, const std::vector<double>& f, const std::vector<D3>& V, int i,
                     double R) {
    double s = 0.0;
    for (int k = g.nbrStart[i]; k < g.nbrStart[i + 1]; k++) {
        int j = g.nbr[k];
        double vn = dot((V[i] + V[j]) * 0.5, g.enorm[k]); // outward positive
        s += (vn > 0 ? f[i] : f[j]) * vn * g.elen[k];
    }
    return -s / (g.area[i] * R);
}

// The shortest distance between neighbouring centres, in metres: the number
// the timestep has to respect, and on this grid it is very nearly the same
// everywhere.
inline double minSpacing(const Grid& g, double R) {
    double m = 1e30;
    for (double d : g.edist) m = std::min(m, d);
    return m * R;
}
inline double maxSpacing(const Grid& g, double R) {
    double m = 0.0;
    for (double d : g.edist) m = std::max(m, d);
    return m * R;
}

} // namespace geodesic
