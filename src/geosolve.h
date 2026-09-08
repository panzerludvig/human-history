// The elliptic solver on the geodesic grid: (lap - kappa) psi = rhs.
//
// The quasi-geostrophic weather recovers its streamfunctions from potential
// vorticity by inverting a Poisson equation (the barotropic part) and a
// Helmholtz equation (the baroclinic part) every stage of every step. On the
// latitude-longitude grid that was a Fourier transform in longitude and a
// tridiagonal solve in latitude, which is exact and cheap and only exists
// on a grid with rows. This is its replacement, and it is what lets the
// channels, the walls and the polar filter go.
//
// The mesh Laplacian (geodesic::lap) is, times the cell's area, a sum of
// (psi_j - psi_i) weighted by edge length over centre distance. Those
// weights are the same seen from either side of an edge, so the matrix
//     M = L - kappa A     with A the diagonal of cell areas
// is symmetric and negative definite (semi-definite when kappa is zero, the
// constant being invisible to a Laplacian). Conjugate gradient on -M with a
// Jacobi preconditioner solves it; for kappa = 0 the right-hand side's
// area-weighted mean is removed first, which is the compatibility condition
// on a closed surface, and the answer's mean is removed after.
//
// Warm starts matter: between one Runge-Kutta stage and the next the field
// barely changes, so the previous psi is passed in. What Jacobi alone cannot
// do is move the planetary-scale part of the answer, which conjugate
// gradient carries one cell per iteration: 124 iterations warm for the
// Poisson case, 13 minutes a year. So the preconditioner has a second
// level: the cells are aggregated onto a mesh two subdivisions coarser (642
// aggregates at level 5), the Galerkin coarse operator is factored once by
// dense Cholesky per coupling constant, and each application adds the
// coarse correction to the Jacobi one. The test (solvetest.cpp) measures
// all of it.
#pragma once
#include <cmath>
#include <vector>
#include "geodesic.h"

namespace geodesic {

struct Solver {
    const Grid* g = nullptr;
    double R = 1.0;
    std::vector<double> w;    // per neighbour slot: elen / edist, dimensionless
    std::vector<double> a;    // per cell: area * R^2, m^2
    std::vector<double> wsum; // per cell: the sum of its edge weights
    std::vector<double> r, z, p, Ap;
    int lastIterations = 0;
    double lastResidual = 0;
    // the coarse level
    Grid coarse;
    int Nc = 0;
    std::vector<int> agg;                 // fine cell -> aggregate
    std::vector<double> aggArea;          // sum of a over the aggregate
    std::vector<std::vector<std::pair<int, double>>> cW; // aggregate graph: (neighbour aggregate, summed weight)
    std::vector<double> rc, zc;           // coarse work
    struct Factor { double kappa; std::vector<double> L; }; // dense Cholesky, row-major Nc x Nc
    std::vector<Factor> factors;
    bool twoLevel = true;
    int coarseDrop = 3;                   // subdivision levels between the fine mesh and the aggregates

    void init(const Grid& grid, double radius, int drop = 3) {
        coarseDrop = drop;
        g = &grid; R = radius;
        int N = g->size();
        w.resize(g->nbrStart[N]);
        for (int k = 0; k < g->nbrStart[N]; k++) w[k] = g->elen[k] / g->edist[k];
        a.resize(N); wsum.assign(N, 0.0);
        for (int i = 0; i < N; i++) {
            a[i] = g->area[i] * R * R;
            for (int k = g->nbrStart[i]; k < g->nbrStart[i + 1]; k++) wsum[i] += w[k];
        }
        r.assign(N, 0.0); z.assign(N, 0.0); p.assign(N, 0.0); Ap.assign(N, 0.0);
        // the coarse level: nearest coarse centre
        int cl = std::max(g->level - coarseDrop, 0);
        coarse = build(cl);
        Nc = coarse.size();
        agg.assign(N, 0); aggArea.assign(Nc, 0.0);
        for (int i = 0; i < N; i++) {
            int best = 0; double bd = -2;
            for (int I = 0; I < Nc; I++) { double d = dot(g->c[i], coarse.c[I]); if (d > bd) { bd = d; best = I; } }
            agg[i] = best; aggArea[best] += a[i];
        }
        cW.assign(Nc, {});
        for (int i = 0; i < N; i++)
            for (int k = g->nbrStart[i]; k < g->nbrStart[i + 1]; k++) {
                int I = agg[i], J = agg[g->nbr[k]];
                if (I == J) continue;
                bool found = false;
                for (auto& e : cW[I]) if (e.first == J) { e.second += w[k]; found = true; break; }
                if (!found) cW[I].push_back({J, w[k]});
            }
        rc.assign(Nc, 0.0); zc.assign(Nc, 0.0);
        factors.clear();
    }

    // The coarse operator for this kappa, factored: (R (-M) P) with P
    // piecewise constant over the aggregates, plus a small shift so the
    // Poisson case, singular on a closed surface, still factors.
    const Factor& factorFor(double kappa) {
        for (auto& f : factors) if (f.kappa == kappa) return f;
        Factor f; f.kappa = kappa; f.L.assign((size_t)Nc * Nc, 0.0);
        std::vector<double>& A = f.L;
        double maxDiag = 0;
        for (int I = 0; I < Nc; I++) {
            double d = kappa * aggArea[I];
            for (auto& e : cW[I]) { d += e.second; A[(size_t)I * Nc + e.first] = -e.second; }
            A[(size_t)I * Nc + I] = d; maxDiag = std::max(maxDiag, d);
        }
        for (int I = 0; I < Nc; I++) A[(size_t)I * Nc + I] += 1e-9 * maxDiag;
        // Cholesky in place (lower triangle)
        for (int j = 0; j < Nc; j++) {
            double d = A[(size_t)j * Nc + j];
            for (int k = 0; k < j; k++) d -= A[(size_t)j * Nc + k] * A[(size_t)j * Nc + k];
            d = std::sqrt(std::max(d, 1e-300));
            A[(size_t)j * Nc + j] = d;
            for (int i = j + 1; i < Nc; i++) {
                double s = A[(size_t)i * Nc + j];
                for (int k = 0; k < j; k++) s -= A[(size_t)i * Nc + k] * A[(size_t)j * Nc + k];
                A[(size_t)i * Nc + j] = s / d;
            }
        }
        factors.push_back(std::move(f));
        return factors.back();
    }

    // z = D^-1 r + P Ac^-1 R r
    void precondition(const std::vector<double>& rr, std::vector<double>& zz, double kappa, const Factor* F) {
        int N = g->size();
        for (int i = 0; i < N; i++) zz[i] = rr[i] / (wsum[i] + kappa * a[i]);
        if (!F) return;
        std::fill(rc.begin(), rc.end(), 0.0);
        for (int i = 0; i < N; i++) rc[agg[i]] += rr[i];
        const std::vector<double>& L = F->L;
        for (int i = 0; i < Nc; i++) {           // forward: L y = rc
            double s = rc[i];
            for (int k = 0; k < i; k++) s -= L[(size_t)i * Nc + k] * zc[k];
            zc[i] = s / L[(size_t)i * Nc + i];
        }
        for (int i = Nc - 1; i >= 0; i--) {      // backward: L^T x = y
            double s = zc[i];
            for (int k = i + 1; k < Nc; k++) s -= L[(size_t)k * Nc + i] * zc[k];
            zc[i] = s / L[(size_t)i * Nc + i];
        }
        for (int i = 0; i < N; i++) zz[i] += zc[agg[i]];
    }

    // y = -M x = sum_k w_k (x_i - x_j) + kappa a_i x_i
    void apply(const std::vector<double>& x, std::vector<double>& y, double kappa) const {
        int N = g->size();
#pragma omp parallel for
        for (int i = 0; i < N; i++) {
            double s = wsum[i] * x[i];
            for (int k = g->nbrStart[i]; k < g->nbrStart[i + 1]; k++) s -= w[k] * x[g->nbr[k]];
            y[i] = s + kappa * a[i] * x[i];
        }
    }

    double areaMean(const std::vector<double>& x) const {
        double s = 0, t = 0;
        for (int i = 0; i < g->size(); i++) { s += x[i] * a[i]; t += a[i]; }
        return s / t;
    }

    // Solves lap(psi) - kappa psi = rhs, starting from the psi passed in.
    // tol is relative to the right-hand side's norm. Returns the iterations.
    int solve(const std::vector<double>& rhs, double kappa, std::vector<double>& psi, double tol = 1e-7, int maxIter = 5000) {
        int N = g->size();
        // b = -a rhs, with the compatibility condition when kappa is zero
        std::vector<double> b(N);
        double m = kappa == 0.0 ? areaMean(rhs) : 0.0;
        for (int i = 0; i < N; i++) b[i] = -a[i] * (rhs[i] - m);
        double bn = 0; for (int i = 0; i < N; i++) bn += b[i] * b[i]; bn = std::sqrt(bn);
        if (bn == 0) { std::fill(psi.begin(), psi.end(), 0.0); lastIterations = 0; lastResidual = 0; return 0; }
        const Factor* F = twoLevel ? &factorFor(kappa) : nullptr;
        apply(psi, Ap, kappa);
        for (int i = 0; i < N; i++) r[i] = b[i] - Ap[i];
        precondition(r, z, kappa, F);
        double rz = 0;
        for (int i = 0; i < N; i++) { p[i] = z[i]; rz += r[i] * z[i]; }
        int it = 0;
        double rn = 0;
        for (; it < maxIter; it++) {
            rn = 0; for (int i = 0; i < N; i++) rn += r[i] * r[i]; rn = std::sqrt(rn);
            if (rn <= tol * bn) break;
            apply(p, Ap, kappa);
            double pAp = 0; for (int i = 0; i < N; i++) pAp += p[i] * Ap[i];
            double alpha = rz / pAp;
            for (int i = 0; i < N; i++) { psi[i] += alpha * p[i]; r[i] -= alpha * Ap[i]; }
            precondition(r, z, kappa, F);
            double rzNew = 0;
            for (int i = 0; i < N; i++) rzNew += r[i] * z[i];
            double beta = rzNew / rz; rz = rzNew;
            for (int i = 0; i < N; i++) p[i] = z[i] + beta * p[i];
        }
        if (kappa == 0.0) { double pm = areaMean(psi); for (int i = 0; i < N; i++) psi[i] -= pm; }
        lastIterations = it; lastResidual = rn / bn;
        return it;
    }
};

} // namespace geodesic
