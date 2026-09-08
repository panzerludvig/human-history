// The two-layer quasi-geostrophic atmosphere on the geodesic grid.
//
// The same model as qg2.h -- two layers of potential vorticity, relaxed
// toward the thickness the painted climate implies, drag on the lower
// layer, terrain in the lower layer's vorticity -- with the discretisation
// changed and everything the latitude-longitude grid had made necessary
// gone: no channels, no walls, no free-slip condition for the mean flow,
// no row taper, no polar filter. The sphere is one domain.
//
// What stays hard is the equator, where the approximation itself fails.
// The thickness target is referenced to the equator's column temperature,
// so it is zero there and the hemisphere's sign on 1/f0 flips nothing (a
// latitude factor on 1/f0 instead had a gradient of its own that confined
// the storms poleward of 45 degrees), and inside the tropics the drag and
// the relaxation are strengthened so the flow there stays small and
// bounded; the water does not use it there anyway (the belts stay painted
// equatorward of QG_EQ, blended as before).
//
// Advection is in flux form with an exactly nondivergent face flux: the
// flow across the edge between two cells is the difference of the
// streamfunction at the edge's two corners, each corner value the mean of
// the three cells meeting there. Those corners are shared, so the fluxes
// telescope to zero around every cell and cancel exactly across every
// edge -- mass is conserved to rounding, whatever the wind. The face value
// of q is centred, and a small Laplacian viscosity plus third-order
// Runge-Kutta keeps that stable.
//
// Inversion by geosolve.h: a Poisson solve for the barotropic part and a
// Helmholtz solve for the baroclinic one, warm-started from the previous
// stage at a tolerance of 1e-5.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include "geodesic.h"
#include "geosolve.h"

namespace qg2geo {

constexpr double A_EARTH = 6371000.0;
constexpr double OMEGA = 7.292e-5;
constexpr double R_GAS = 287.0;
constexpr double PI = 3.14159265358979323846;

inline int LEVEL = 5;                       // mesh subdivision: 10242 cells, about 250 km
inline double QG_EQ = 20.0;                 // degrees: the tropics, where the QG wind is not used and is damped
inline double TROP_RAMP = 6.0;              // degrees over which the tropical damping fades in
inline double TROP_DRAG = 4.0;              // extra drag in the tropics, as a multiple
inline double TROP_RELAX = 4.0;             // extra relaxation in the tropics, as a multiple
inline double LAT0 = 45.0;                  // degrees: where f0 is taken
inline double H_LAYER = 5000.0;             // m, each layer
inline double G_REDUCED = 1.0;              // m/s2
inline double TAU_RELAX = 20.0 * 86400.0;   // s
inline double EKMAN = 1.0 / (10.0 * 86400.0); // s^-1
inline double VISC = 3.0e4;                 // m2/s
inline double DT = 1800.0;                  // s
inline double TERRAIN_SCALE = 1.0;
inline double TERRAIN_CAP = 2000.0;         // m
inline double ANOM_SHARE = 0.6;
inline double TARGET_GAIN = 1.5;
inline double SOLVE_TOL = 1e-5;
inline int SMOOTH_PASSES = 6;
inline double ZONAL_BAND = 2.0;             // degrees: the latitude bands the zonal means use

struct Model {
    geodesic::Grid g;
    geodesic::Solver solver;
    int N = 0;
    std::vector<double> q1, q2, psi1, psi2, psib, psic, psicT, topo, f, invF0, trop, lat, lon;
    std::vector<geodesic::D3> east, north;
    std::vector<double> tmpA, tmpB, k1a, k1b, k2a, k2b, rhs;
    std::vector<geodesic::D3> V1, V2;       // the winds, tangent vectors, m/s
    std::vector<double> u1, v1, u2, v2;     // and their east/north components
    std::vector<int> band;                  // latitude band per cell
    std::vector<double> bandSum, bandN;
    int nBands = 0;
    double fluxSign = 1.0, F = 0, f0 = 0;
    // probes
    std::vector<double> ekeAcc, u2Acc, v2Acc, u1Acc, psiAcc;
    double hoursBanked = 0;
    int solveIterations = 0;

    // The column's mean temperature from the near-surface air, as Earth's
    // 1000-500 hPa thickness climatology has it (winter zonal means, the
    // thickness divided by R ln2 / g = 20.3 m per kelvin): the tropical
    // column is nearly uniform whatever the local surface, because deep
    // convection sets it from the warmest sea, and the polar column is
    // warmer than its surface under the inversion. The slope peaks near
    // one between 3 and 15 degrees and is 0.55-0.67 either side, which is
    // what puts the thickness gradient, and the storms, at 30-50 degrees.
    // A lapse-rate model with a strong moist-adiabatic slope put the
    // gradient at the polar front instead and the storm track at 60-88.
    static double columnT(double ts) {
        static const double TS[] = {-40, -30, -12, 3, 15, 26, 32};
        static const double TC[] = {-33, -27, -17, -7, 5.3, 12.7, 14.5};
        const int n = 7;
        if (ts <= TS[0]) return TC[0] + (ts - TS[0]) * 0.6;
        if (ts >= TS[n - 1]) return TC[n - 1] + (ts - TS[n - 1]) * 0.3;
        for (int k = 0; k < n - 1; k++)
            if (ts <= TS[k + 1]) return TC[k] + (TC[k + 1] - TC[k]) * (ts - TS[k]) / (TS[k + 1] - TS[k]);
        return TC[n - 1];
    }

    // elev and water per mesh cell (empty: a water world)
    void init(const std::vector<float>& elev, const std::vector<unsigned char>& water) {
        g = geodesic::build(LEVEL);
        N = g.size();
        solver.init(g, A_EARTH, 3);
        for (auto* v : {&q1, &q2, &psi1, &psi2, &psib, &psic, &psicT, &topo, &f, &invF0, &trop, &lat, &lon,
                        &tmpA, &tmpB, &k1a, &k1b, &k2a, &k2b, &rhs, &u1, &v1, &u2, &v2,
                        &ekeAcc, &u2Acc, &v2Acc, &u1Acc, &psiAcc})
            v->assign(N, 0.0);
        V1.assign(N, {0, 0, 0}); V2.assign(N, {0, 0, 0}); east.resize(N); north.resize(N);
        f0 = 2 * OMEGA * std::sin(LAT0 * PI / 180.0);
        F = f0 * f0 / (G_REDUCED * H_LAYER);
        double s45 = std::sin(LAT0 * PI / 180.0);
        nBands = (int)(180.0 / ZONAL_BAND);
        band.assign(N, 0); bandSum.assign(nBands, 0.0); bandN.assign(nBands, 0.0);
        for (int i = 0; i < N; i++) {
            const geodesic::D3& c = g.c[i];
            lat[i] = std::asin(std::clamp(c.z, -1.0, 1.0));
            lon[i] = std::atan2(c.y, c.x);
            f[i] = 2 * OMEGA * c.z;
            // The hemisphere's sign on 1/f0. The target vanishes at the
            // equator because its column temperature is referenced to the
            // equator's (see setTargets), so nothing flips there; a latitude
            // factor here instead (sin, then sin cubed) had a gradient of
            // its own that, times a warm subtropical column, made easterly
            // shear from 10 to 45 degrees and confined the storms poleward.
            invF0[i] = (c.z >= 0 ? 1.0 : -1.0) / f0;
            double la = std::fabs(lat[i]) * 180.0 / PI;
            trop[i] = std::clamp((QG_EQ - la) / TROP_RAMP + 1.0, 0.0, 1.0);   // 1 inside QG_EQ - TROP_RAMP, 0 beyond QG_EQ
            geodesic::D3 z{0, 0, 1};
            geodesic::D3 e = geodesic::cross(z, c);
            double el = geodesic::len(e);
            east[i] = el > 1e-9 ? e * (1.0 / el) : geodesic::D3{0, 1, 0};
            north[i] = geodesic::cross(c, east[i]);
            band[i] = std::clamp((int)((la * (lat[i] < 0 ? -1 : 1) + 90.0) / ZONAL_BAND), 0, nBands - 1);
            bool w = water.empty() ? true : water[i] != 0;
            topo[i] = w ? 0.0 : std::min((double)(elev.empty() ? 0.0f : elev[i]), TERRAIN_CAP) * TERRAIN_SCALE;
        }
        smooth(topo, SMOOTH_PASSES);
        // The sign of the corner-difference flux, settled once against the
        // geometric flux of a solid-body rotation.
        std::vector<double> ps(N);
        for (int i = 0; i < N; i++) ps[i] = -10.0 * A_EARTH * g.c[i].z;
        std::vector<geodesic::D3> V(N);
        for (int i = 0; i < N; i++) V[i] = geodesic::cross(g.c[i], geodesic::grad(g, ps, i, A_EARTH));
        double corr = 0;
        for (int i = 0; i < N; i++) {
            int d = g.deg(i), b = g.nbrStart[i];
            for (int k = 0; k < d; k++) {
                int j = g.nbr[b + k];
                double geo = geodesic::dot((V[i] + V[j]) * 0.5, g.enorm[b + k]) * g.elen[b + k] * A_EARTH;
                corr += geo * cornerFlux(ps, i, k);
            }
        }
        fluxSign = corr >= 0 ? 1.0 : -1.0;
    }

    // The flow out of cell i across its k-th edge, m2/s: the difference of
    // the streamfunction between the edge's corners, each the mean of the
    // three cells meeting there (i, neighbour k, and neighbour k+1 or k-1).
    double cornerFlux(const std::vector<double>& p, int i, int k) const {
        int d = g.deg(i), b = g.nbrStart[i];
        int jn = g.nbr[b + (k + 1) % d], jp = g.nbr[b + (k + d - 1) % d];
        return fluxSign * (p[jn] - p[jp]) / 3.0;
    }

    void smooth(std::vector<double>& x, int passes) {
        for (int pass = 0; pass < passes; pass++) {
            for (int i = 0; i < N; i++) {
                double s = 0; int d = g.deg(i);
                for (int k = g.nbrStart[i]; k < g.nbrStart[i + 1]; k++) s += x[g.nbr[k]];
                tmpA[i] = 0.4 * x[i] + 0.6 * s / d;
            }
            x = tmpA;
        }
    }

    void zonalMean(const std::vector<double>& x, std::vector<double>& out) {
        std::fill(bandSum.begin(), bandSum.end(), 0.0); std::fill(bandN.begin(), bandN.end(), 0.0);
        for (int i = 0; i < N; i++) { bandSum[band[i]] += x[i] * g.area[i]; bandN[band[i]] += g.area[i]; }
        for (int i = 0; i < N; i++) out[i] = bandN[band[i]] > 0 ? bandSum[band[i]] / bandN[band[i]] : 0.0;
    }

    void setTargets(const std::vector<double>& tNearSurfaceC, bool first) {
        for (int i = 0; i < N; i++) tmpB[i] = columnT(tNearSurfaceC[i]);
        zonalMean(tmpB, rhs);
        // The thermal wind divides the temperature gradient by the local f,
        // not the column by one f0: psi_c(lat) is the integral from the
        // equator of (R ln2 / 2f) dT, band by band, with |f| held at its
        // 20-degree value inside the tropics. One f0 at 45 degrees gave
        // the subtropics 0.7 of their shear and the polar cap 1.3 times
        // its own, and put the surface westerlies 15 degrees too far
        // poleward. The target is zero at the equator by construction.
        std::vector<double> zmBand(nBands, 0.0), cum(nBands, 0.0);
        for (int b = 0; b < nBands; b++) zmBand[b] = bandN[b] > 0 ? bandSum[b] / bandN[b] : 0.0;
        double fMin = 2 * OMEGA * std::sin(QG_EQ * PI / 180.0);
        double coef = 0.5 * R_GAS * 0.6931 * TARGET_GAIN;
        int eq = nBands / 2;   // the first band north of the equator
        for (int b = eq; b < nBands; b++) {
            double la = (-90.0 + (b + 0.5) * ZONAL_BAND) * PI / 180.0;
            double fb = std::max(2 * OMEGA * std::sin(la), fMin);
            double dT = b == eq ? 0.0 : zmBand[b] - zmBand[b - 1];
            cum[b] = (b == eq ? 0.0 : cum[b - 1]) + coef * dT / fb;
        }
        for (int b = eq - 1; b >= 0; b--) {
            double la = (-90.0 + (b + 0.5) * ZONAL_BAND) * PI / 180.0;
            double fb = std::min(2 * OMEGA * std::sin(la), -fMin);
            double dT = zmBand[b] - zmBand[b + 1];
            cum[b] = cum[b + 1] + coef * dT / fb;
        }
        for (int i = 0; i < N; i++) {
            double fl = f[i] >= 0 ? std::max(f[i], fMin) : std::min(f[i], -fMin);
            psicT[i] = cum[band[i]] + coef * ANOM_SHARE * (tmpB[i] - rhs[i]) * (1.0 - trop[i]) / fl;
        }
        smooth(psicT, SMOOTH_PASSES);
        if (first) {
            zonalMean(psicT, rhs);
            for (int i = 0; i < N; i++) { psi1[i] = rhs[i]; psi2[i] = -rhs[i]; psib[i] = 0; psic[i] = rhs[i]; }
            computeQ();
        }
    }

    void computeQ() {
        for (int i = 0; i < N; i++) {
            q1[i] = geodesic::lap(g, psi1, i, A_EARTH) + f[i] + F * (psi2[i] - psi1[i]);
            q2[i] = geodesic::lap(g, psi2, i, A_EARTH) + f[i] + F * (psi1[i] - psi2[i]) + (1.0 - trop[i]) * invF0[i] * f0 * f0 * topo[i] / H_LAYER;
        }
    }

    void invert() {
        for (int i = 0; i < N; i++) {
            double tp = (1.0 - trop[i]) * invF0[i] * f0 * f0 * topo[i] / H_LAYER;
            rhs[i] = 0.5 * (q1[i] + q2[i]) - f[i] - 0.5 * tp;
        }
        solveIterations = solver.solve(rhs, 0.0, psib, SOLVE_TOL);
        for (int i = 0; i < N; i++) {
            double tp = (1.0 - trop[i]) * invF0[i] * f0 * f0 * topo[i] / H_LAYER;
            rhs[i] = 0.5 * (q1[i] - q2[i]) - 0.5 * tp;
        }
        solveIterations += solver.solve(rhs, 2 * F, psic, SOLVE_TOL);
        for (int i = 0; i < N; i++) { psi1[i] = psib[i] + psic[i]; psi2[i] = psib[i] - psic[i]; }
    }

    // -(1/a) sum_k q_face flux_k, with the exactly nondivergent corner flux
    double advection(const std::vector<double>& p, const std::vector<double>& q, int i) const {
        double s = 0; int d = g.deg(i), b = g.nbrStart[i];
        for (int k = 0; k < d; k++) s += 0.5 * (q[i] + q[g.nbr[b + k]]) * cornerFlux(p, i, k);
        return -s / (g.area[i] * A_EARTH * A_EARTH);
    }

    void tendency(std::vector<double>& dq1, std::vector<double>& dq2) {
#pragma omp parallel for
        for (int i = 0; i < N; i++) {
            double pc = 0.5 * (psi1[i] - psi2[i]);
            double heat = 2 * F * (psicT[i] - pc) / TAU_RELAX * (1.0 + TROP_RELAX * trop[i]);
            double zeta2 = geodesic::lap(g, psi2, i, A_EARTH);
            dq1[i] = -advection(psi1, q1, i) - heat + VISC * geodesic::lap(g, q1, i, A_EARTH);
            dq2[i] = -advection(psi2, q2, i) + heat - EKMAN * (1.0 + TROP_DRAG * trop[i]) * zeta2 + VISC * geodesic::lap(g, q2, i, A_EARTH);
        }
    }

    void winds() {
        for (int i = 0; i < N; i++) {
            V1[i] = geodesic::cross(g.c[i], geodesic::grad(g, psi1, i, A_EARTH));
            V2[i] = geodesic::cross(g.c[i], geodesic::grad(g, psi2, i, A_EARTH));
            u1[i] = geodesic::dot(V1[i], east[i]); v1[i] = geodesic::dot(V1[i], north[i]);
            u2[i] = geodesic::dot(V2[i], east[i]); v2[i] = geodesic::dot(V2[i], north[i]);
        }
    }

    // one step, third-order strong-stability-preserving Runge-Kutta
    void step(double dt) {
        std::vector<double>& s1 = tmpA; std::vector<double>& s2 = tmpB;
        s1 = q1; s2 = q2;
        invert(); tendency(k1a, k1b);
        for (int i = 0; i < N; i++) { q1[i] = s1[i] + dt * k1a[i]; q2[i] = s2[i] + dt * k1b[i]; }
        invert(); tendency(k2a, k2b);
        for (int i = 0; i < N; i++) {
            q1[i] = 0.75 * s1[i] + 0.25 * (q1[i] + dt * k2a[i]);
            q2[i] = 0.75 * s2[i] + 0.25 * (q2[i] + dt * k2b[i]);
        }
        invert(); tendency(k1a, k1b);
        for (int i = 0; i < N; i++) {
            q1[i] = (s1[i] + 2.0 * (q1[i] + dt * k1a[i])) / 3.0;
            q2[i] = (s2[i] + 2.0 * (q2[i] + dt * k1b[i])) / 3.0;
        }
        invert();
        winds();
    }

    void bank() {
        zonalMean(u2, k2a); zonalMean(v2, k2b);
        for (int i = 0; i < N; i++) {
            ekeAcc[i] += 0.5 * ((u2[i] - k2a[i]) * (u2[i] - k2a[i]) + (v2[i] - k2b[i]) * (v2[i] - k2b[i]));
            u2Acc[i] += u2[i]; v2Acc[i] += v2[i]; u1Acc[i] += u1[i]; psiAcc[i] += psi2[i];
        }
        hoursBanked += 1;
    }
};

} // namespace qg2geo
