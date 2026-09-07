// Two-layer quasi-geostrophic atmosphere: the cheapest thing that makes
// real weather.
//
// Gravity waves are filtered out by construction, so the step is half an
// hour and a year runs in a minute or two; baroclinic instability is
// guaranteed by the theory (Phillips 1954) rather than hoped for; terrain
// enters as a forcing on the lower layer's vorticity; and the painted
// climate enters as a relaxation of the layers' thickness difference
// toward the thermal wind it implies. The approximation fails near the
// equator, so the model lives in two channels, one per hemisphere, between
// the latitudes QG_EQ and QG_CAP, with rigid walls at both edges; the
// tropics stay painted.
//
// State per layer k (1 = upper, 2 = lower): the potential vorticity q_k.
// Streamfunctions psi_k follow by inversion each step:
//   q1 = lap(psi1) + f + F (psi2 - psi1)
//   q2 = lap(psi2) + f + F (psi1 - psi2) + f0 h_b / H
// decomposed into a barotropic part, a Poisson equation, and a baroclinic
// part, a Helmholtz equation, each solved exactly by a discrete Fourier
// transform in longitude and a tridiagonal solve in latitude. Advection
// is Arakawa's nine-point Jacobian, which conserves energy and enstrophy
// and does not blow up; time stepping is third-order strong-stability-
// preserving Runge-Kutta (second order went unstable). The zonal-mean
// flow sees a free-slip poleward wall and a pinned equatorward one, so
// the westerlies need not be paid for with easterlies across the channel.
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <xmmintrin.h>
#include <pmmintrin.h>

namespace qg2 {

constexpr double A_EARTH = 6371000.0;
constexpr double OMEGA = 7.292e-5;
constexpr double R_GAS = 287.0;
inline double QG_EQ = 20.0;          // degrees: the channels' equatorward wall
inline double QG_CAP = 80.0;         // and their poleward wall
inline double LAT0 = 45.0;           // degrees: where f0 is taken
inline double H_LAYER = 5000.0;      // m, each layer
inline double G_REDUCED = 1.0;       // m/s2: g * delta-theta / theta between the layers
inline double TAU_RELAX = 20.0 * 86400.0;   // s, thickness relaxes to the painted climate (8 days damped the baroclinic wave)
inline double EKMAN = 1.0 / (10.0 * 86400.0); // s^-1, drag on the lower layer's vorticity (5 days held the wave below its growth)
inline double VISC = 3.0e4;          // m2/s, Laplacian diffusion of q
inline double DT = 1800.0;           // s
inline double TERRAIN_SCALE = 1.0;   // share of the (capped, smoothed) terrain in the lower layer's forcing
inline double TERRAIN_CAP = 2000.0;  // m
inline double ANOM_SHARE = 0.6;      // of the surface temperature anomaly that the thickness target carries
// The column from its surface. The lower layer's mean temperature is
// not the surface's: over a warm sea the moist adiabat holds the column
// within a few degrees of the surface while over a winter continent an
// inversion sits under air warmer than the ground, so the column's
// equator-to-pole contrast is split about evenly at 45 degrees where
// the surface's is weighted to the polar front (Earth's 1000-500 hPa
// thickness: 5750 m tropics, 5400 at 45 degrees, 5000 at the pole). A
// lapse rate that peaks at LAPSE_PEAK_T and falls off both ways gives
// that, and took the storm tracks from 70 degrees back toward 50.
inline double LAPSE_MAX = 6.5;       // K/km, at LAPSE_PEAK_T
inline double LAPSE_PEAK_T = 15.0;   // degC, near-surface
inline double LAPSE_WARM = 0.35;     // K/km lost per degree warmer (the moist adiabat)
inline double LAPSE_COLD = 0.18;     // K/km lost per degree colder (the inversion)
inline double COLUMN_MID = 2.5;      // km, the lower layer's mid-height
inline double TARGET_GAIN = 1.5;     // the 2.5-7.5 km layer's contrast over the 0-5 km column's, which the lapse rates were fitted to
inline double FILTER_U = 80.0;       // m/s: the zonal filter keeps the waves this wind can carry stably in one step
inline double FILTER_CFL = 1.5;      // the advection scheme's stability limit, wind * dt * wavenumber

struct Model {
    int W = 0, H = 0;
    std::vector<double> q1, q2, psi1, psi2, psicT, topo, f, cosl, lat;  // fields
    std::vector<double> tmpA, tmpB, k1a, k1b, k2a, k2b, colBuf;         // work
    std::vector<double> u1, v1, u2, v2;                                  // diagnosed winds
    std::vector<double> dftC, dftS;                                      // cos/sin tables [m][x]
    double f0N = 0, f0S = 0, F = 0;
    // the channels: rows [nS0, nS1) in the south, [nN0, nN1) in the north
    int nS0 = 0, nS1 = 0, nN0 = 0, nN1 = 0;
    // probes
    std::vector<double> ekeAcc, u2Acc, v2Acc, u1Acc, psiAcc;
    double hoursBanked = 0;

    int idx(int x, int y) const { return y * W + x; }
    int wrapX(int x) const { return (x % W + W) % W; }
    bool inChannel(int y) const { return (y >= nS0 && y < nS1) || (y >= nN0 && y < nN1); }
    double f0At(int y) const { return y < H / 2 ? f0S : f0N; }

    void init(int w, int h, const std::vector<float>& elev, const std::vector<float>& latRad,
              const std::vector<unsigned char>& water) {
        W = w; H = h;
        // Denormals: the Fourier sums of a nearly zonal field make values
        // near 1e-300 that the processor handles in microcode at a hundredth
        // of its speed -- a forty-day run took 27 minutes at 10 ms a step.
        _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
        _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
        int n = W * H;
        for (auto* v : {&q1, &q2, &psi1, &psi2, &psicT, &topo, &f, &cosl, &lat, &tmpA, &tmpB, &k1a, &k1b, &k2a, &k2b,
                        &u1, &v1, &u2, &v2, &ekeAcc, &u2Acc, &v2Acc, &u1Acc, &psiAcc})
            v->assign(n, 0.0);
        f0N = 2 * OMEGA * std::sin(LAT0 * 3.14159265 / 180.0);
        f0S = -f0N;
        F = f0N * f0N / (G_REDUCED * H_LAYER);
        nS0 = nS1 = nN0 = nN1 = -1;
        for (int y = 0; y < H; y++) {
            double la = ((y + 0.5) / H - 0.5) * 180.0;
            double ala = std::fabs(la);
            bool in = ala >= QG_EQ && ala <= QG_CAP;
            if (la < 0) { if (in && nS0 < 0) nS0 = y; if (in) nS1 = y + 1; }
            else { if (in && nN0 < 0) nN0 = y; if (in) nN1 = y + 1; }
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                lat[i] = latRad[i];
                cosl[i] = std::max(std::cos((double)latRad[i]), 0.05);
                f[i] = 2 * OMEGA * std::sin(latRad[i]);
                topo[i] = water[i] ? 0.0 : std::min((double)elev[i], TERRAIN_CAP) * TERRAIN_SCALE;
            }
        }
        // smooth the terrain twice: the forcing is the range, not the peak
        for (int pass = 0; pass < 2; pass++) {
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++) {
                    int i = idx(x, y), yn = std::min(y + 1, H - 1), ys = std::max(y - 1, 0);
                    tmpA[i] = 0.4 * topo[i] + 0.15 * (topo[idx(wrapX(x + 1), y)] + topo[idx(wrapX(x - 1), y)] +
                                                     topo[idx(x, yn)] + topo[idx(x, ys)]);
                }
            topo = tmpA;
        }
        // DFT tables
        int M = W / 2 + 1;
        dftC.assign((size_t)M * W, 0.0); dftS.assign((size_t)M * W, 0.0);
        for (int m = 0; m < M; m++)
            for (int x = 0; x < W; x++) {
                dftC[(size_t)m * W + x] = std::cos(2 * 3.14159265 * m * x / (double)W);
                dftS[(size_t)m * W + x] = std::sin(2 * 3.14159265 * m * x / (double)W);
            }
    }

    // The painted climate as a thickness target: the lower layer is thick
    // where the near-surface air is warm (hypsometry, R ln2 per kelvin),
    // and the baroclinic streamfunction it implies is that thickness times
    // g over f0, halved. A share of the anomaly, since the surface's
    // contrasts fade with height; smoothed, since a coast is not a climate.
    static double columnT(double ts) {
        double lapse = LAPSE_MAX - LAPSE_WARM * std::max(ts - LAPSE_PEAK_T, 0.0) - LAPSE_COLD * std::max(LAPSE_PEAK_T - ts, 0.0);
        return ts - COLUMN_MID * std::max(lapse, 0.0);
    }

    void setTargets(const std::vector<double>& tNearSurfaceC, bool first) {
        if (colBuf.size() != tNearSurfaceC.size()) colBuf.resize(tNearSurfaceC.size());
        for (size_t i = 0; i < tNearSurfaceC.size(); i++) colBuf[i] = columnT(tNearSurfaceC[i]);
        for (int y = 0; y < H; y++) {
            double zm = 0;
            for (int x = 0; x < W; x++) zm += colBuf[idx(x, y)] / W;
            double f0 = f0At(y);
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                double t = zm + ANOM_SHARE * (colBuf[i] - zm);
                psicT[i] = 0.5 * R_GAS * 0.6931 * TARGET_GAIN * t / f0;  // psi1 - psi2 = R ln2 T / f0; psic is half
            }
        }
        for (int pass = 0; pass < 6; pass++) {
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++) {
                    int i = idx(x, y), yn = std::min(y + 1, H - 1), ys = std::max(y - 1, 0);
                    tmpA[i] = 0.4 * psicT[i] + 0.15 * (psicT[idx(wrapX(x + 1), y)] + psicT[idx(wrapX(x - 1), y)] +
                                                       psicT[idx(x, yn)] + psicT[idx(x, ys)]);
                }
            psicT = tmpA;
        }
        // The walls hold psi = 0, so the target must reach zero at them or
        // the edge rows carry a wall jet (measured at 46 m/s). Each channel's
        // target is centred on its mean and shaped by a sine envelope that
        // vanishes at the walls; the thermal wind is thereby confined to the
        // channel's interior, which is where the approximation holds anyway.
        // Referenced to the equatorward edge row's zonal mean, where the
        // mean streamfunction is pinned at zero, and untapered: the
        // poleward wall is free-slip for the mean flow, and a taper at the
        // equatorward one reversed the thickness gradient there and put 39
        // m/s of easterlies aloft at 22 degrees.
        for (int band = 0; band < 2; band++) {
            int a = band ? nN0 : nS0, b = band ? nN1 : nS1;
            int edge = band ? a : b - 1;
            double m = 0;
            for (int x = 0; x < W; x++) m += psicT[idx(x, edge)] / W;
            for (int y = a; y < b; y++) for (int x = 0; x < W; x++) psicT[idx(x, y)] -= m;
        }
        if (first) {
            // start from the zonal-mean target thickness with no vorticity
            for (int y = 0; y < H; y++) {
                double zm = 0;
                for (int x = 0; x < W; x++) zm += psicT[idx(x, y)] / W;
                for (int x = 0; x < W; x++) {
                    int i = idx(x, y);
                    psi1[i] = zm; psi2[i] = -zm;
                }
            }
            computeQ();
        }
    }

    // The streamfunction beyond a wall: zero beyond the equatorward wall,
    // and beyond the poleward wall the edge row's zonal mean -- the
    // free-slip condition the zonal-mean inversion uses, so the operators
    // and the inversion agree (they did not, and the Jacobian saw a jump of
    // 1.7e7 at the wall).
    double ghost(const std::vector<double>& p, int x, int yy) const {
        (void)x;
        int edge = -1;
        if (yy == nN1 || yy == nS0 - 1) edge = (yy == nN1) ? nN1 - 1 : nS0;     // poleward walls
        else return 0.0;                                                          // equatorward walls, and elsewhere
        double m = 0;
        for (int xx = 0; xx < W; xx++) m += p[idx(xx, edge)] / W;
        return m;
    }
    // ---- differential operators on the sphere (rows inside a channel)
    double lap(const std::vector<double>& p, int x, int y) const {
        int i = idx(x, y);
        double dl = 2 * 3.14159265 / W, dp = 3.14159265 / H;
        double c = cosl[i];
        double cN = std::max(std::cos(lat[i] + 0.5 * dp), 0.02), cS = std::max(std::cos(lat[i] - 0.5 * dp), 0.02);
        double pN = inChannel(y + 1) ? p[idx(x, y + 1)] : ghost(p, x, y + 1);
        double pS = inChannel(y - 1) ? p[idx(x, y - 1)] : ghost(p, x, y - 1);
        double d2x = (p[idx(wrapX(x + 1), y)] + p[idx(wrapX(x - 1), y)] - 2 * p[i]) / (dl * dl * c * c);
        double d2y = (cN * (pN - p[i]) - cS * (p[i] - pS)) / (dp * dp * c);
        return (d2x + d2y) / (A_EARTH * A_EARTH);
    }
    // q from psi (the definition)
    void computeQ() {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                if (!inChannel(y)) { q1[i] = q2[i] = f[i]; continue; }
                q1[i] = lap(psi1, x, y) + f[i] + F * (psi2[i] - psi1[i]);
                q2[i] = lap(psi2, x, y) + f[i] + F * (psi1[i] - psi2[i]) + f0At(y) * topo[i] / H_LAYER;
            }
    }

    // ---- the inversion: (lap - kappa) psi = rhs on one channel, walls at
    // both ends, exactly, by DFT in x and tridiagonals in y
    void solveChannel(std::vector<double>& psi, const std::vector<double>& rhs, double kappa, int y0, int y1, bool polewardAtEnd) {
        int ny = y1 - y0;
        if (ny <= 0) return;
        int M = W / 2 + 1;
        double dl = 2 * 3.14159265 / W, dp = 3.14159265 / H;
        std::vector<double> ar(ny), ai(ny), a(ny), b(ny), c(ny), xr(ny), xi(ny);
        std::vector<double> outR((size_t)M * ny), outI((size_t)M * ny);
#pragma omp parallel for
        for (int m = 0; m < M; m++) {
            std::vector<double> rr(ny), ri(ny), dg(ny), lo(ny), up(ny), sr(ny), si(ny);
            for (int j = 0; j < ny; j++) {
                int y = y0 + j;
                double sr0 = 0, si0 = 0;
                for (int x = 0; x < W; x++) {
                    double v = rhs[idx(x, y)];
                    sr0 += v * dftC[(size_t)m * W + x];
                    si0 -= v * dftS[(size_t)m * W + x];
                }
                rr[j] = sr0 / W; ri[j] = si0 / W;
                double la = ((y + 0.5) / H - 0.5) * 3.14159265;
                double cc = std::max(std::cos(la), 0.05);
                double cN = std::max(std::cos(la + 0.5 * dp), 0.02), cS = std::max(std::cos(la - 0.5 * dp), 0.02);
                double a2 = A_EARTH * A_EARTH;
                // zonal: discrete second difference eigenvalue
                double ex = -(2 - 2 * std::cos(m * dl)) / (dl * dl * cc * cc) / a2;
                lo[j] = cS / (dp * dp * cc) / a2;
                up[j] = cN / (dp * dp * cc) / a2;
                dg[j] = -(cN + cS) / (dp * dp * cc) / a2 + ex - kappa;
            }
            // The zonal mean: psi = 0 on both walls would force the zonal
            // wind to integrate to zero across the channel, paying for every
            // westerly with an easterly at the other wall (measured: -39 m/s
            // aloft at 22 degrees). The mean flow gets a free-slip poleward
            // wall instead -- the ghost row mirrors the edge -- and stays
            // pinned at the equatorward one.
            if (m == 0) {
                if (polewardAtEnd) dg[ny - 1] += up[ny - 1];
                else dg[0] += lo[0];
            }
            // Thomas, real and imaginary parts (walls: psi = 0 outside)
            for (int part = 0; part < 2; part++) {
                std::vector<double>& r = part ? ri : rr;
                std::vector<double>& s = part ? si : sr;
                std::vector<double> cp(ny), dpv(ny);
                cp[0] = up[0] / dg[0]; dpv[0] = r[0] / dg[0];
                for (int j = 1; j < ny; j++) {
                    double den = dg[j] - lo[j] * cp[j - 1];
                    cp[j] = up[j] / den;
                    dpv[j] = (r[j] - lo[j] * dpv[j - 1]) / den;
                }
                s[ny - 1] = dpv[ny - 1];
                for (int j = ny - 2; j >= 0; j--) s[j] = dpv[j] - cp[j] * s[j + 1];
            }
            for (int j = 0; j < ny; j++) { outR[(size_t)m * ny + j] = sr[j]; outI[(size_t)m * ny + j] = si[j]; }
        }
        // inverse DFT
#pragma omp parallel for
        for (int j = 0; j < ny; j++) {
            int y = y0 + j;
            for (int x = 0; x < W; x++) {
                double s = 0;
                for (int m = 0; m < M; m++) {
                    double w = (m == 0 || m == W / 2) ? 1.0 : 2.0;
                    s += w * (outR[(size_t)m * ny + j] * dftC[(size_t)m * W + x] - outI[(size_t)m * ny + j] * dftS[(size_t)m * W + x]);
                }
                psi[idx(x, y)] = s;
            }
        }
    }
    void invert() {
        // barotropic: lap(psib) = (q1 + q2)/2 - f - f0 topo/(2H); baroclinic: (lap - 2F) psic = (q1 - q2)/2 + f0 topo/(2H)
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                double tp = f0At(y) * topo[i] / H_LAYER;
                tmpA[i] = 0.5 * (q1[i] + q2[i]) - f[i] - 0.5 * tp;
                tmpB[i] = 0.5 * (q1[i] - q2[i]) + 0.5 * tp;
            }
        std::vector<double> pb(W * H, 0.0), pc(W * H, 0.0);
        for (int band = 0; band < 2; band++) {
            int a = band ? nN0 : nS0, b = band ? nN1 : nS1;
            solveChannel(pb, tmpA, 0.0, a, b, band == 1);
            solveChannel(pc, tmpB, 2 * F, a, b, band == 1);
        }
        for (int i = 0; i < W * H; i++) { psi1[i] = pb[i] + pc[i]; psi2[i] = pb[i] - pc[i]; }
    }

    // Arakawa's nine-point Jacobian J(psi, q) / (a^2 cos), for one cell
    double jacobian(const std::vector<double>& p, const std::vector<double>& q, int x, int y) const {
        double dl = 2 * 3.14159265 / W, dp = 3.14159265 / H;
        auto P = [&](int dx, int dy) { int yy = y + dy; return inChannel(yy) ? p[idx(wrapX(x + dx), yy)] : ghost(p, wrapX(x + dx), yy); };
        auto Q = [&](int dx, int dy) { int yy = y + dy; return inChannel(yy) ? q[idx(wrapX(x + dx), yy)] : q[idx(wrapX(x + dx), y)]; };
        double j1 = ((P(1, 0) - P(-1, 0)) * (Q(0, 1) - Q(0, -1)) - (P(0, 1) - P(0, -1)) * (Q(1, 0) - Q(-1, 0)));
        double j2 = (P(1, 0) * (Q(1, 1) - Q(1, -1)) - P(-1, 0) * (Q(-1, 1) - Q(-1, -1)) -
                     P(0, 1) * (Q(1, 1) - Q(-1, 1)) + P(0, -1) * (Q(1, -1) - Q(-1, -1)));
        double j3 = (Q(0, 1) * (P(1, 1) - P(-1, 1)) - Q(0, -1) * (P(1, -1) - P(-1, -1)) -
                     Q(1, 0) * (P(1, 1) - P(1, -1)) + Q(-1, 0) * (P(-1, 1) - P(-1, -1)));
        double J = (j1 + j2 + j3) / (3.0 * 4.0 * dl * dp);
        return J / (A_EARTH * A_EARTH * cosl[idx(x, y)]);
    }

    // tendencies of q1, q2 from the current psi
    void tendency(std::vector<double>& dq1, std::vector<double>& dq2) {
#pragma omp parallel for
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                if (!inChannel(y)) { dq1[i] = dq2[i] = 0.0; continue; }
                double psic = 0.5 * (psi1[i] - psi2[i]);
                double heat = 2 * F * (psicT[i] - psic) / TAU_RELAX; // thickness toward the painted climate
                double zeta2 = lap(psi2, x, y);
                dq1[i] = -jacobian(psi1, q1, x, y) - heat + VISC * lap(q1, x, y);
                dq2[i] = -jacobian(psi2, q2, x, y) + heat - EKMAN * zeta2 + VISC * lap(q2, x, y);
            }
    }

    void winds() {
        double dl = 2 * 3.14159265 / W, dp = 3.14159265 / H;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                if (!inChannel(y)) { u1[i] = v1[i] = u2[i] = v2[i] = 0.0; continue; }
                auto P = [&](const std::vector<double>& p, int dx, int dy) { int yy = y + dy; return inChannel(yy) ? p[idx(wrapX(x + dx), yy)] : ghost(p, wrapX(x + dx), yy); };
                u1[i] = -(P(psi1, 0, 1) - P(psi1, 0, -1)) / (2 * dp * A_EARTH);
                v1[i] = (P(psi1, 1, 0) - P(psi1, -1, 0)) / (2 * dl * A_EARTH * cosl[i]);
                u2[i] = -(P(psi2, 0, 1) - P(psi2, 0, -1)) / (2 * dp * A_EARTH);
                v2[i] = (P(psi2, 1, 0) - P(psi2, -1, 0)) / (2 * dl * A_EARTH * cosl[i]);
            }
    }

    // The polar filter. A cell at 80 degrees is 36 km wide, and a 70 m/s
    // jet that drifted there crossed two cells a step and blew up in four
    // hours (Earth, day 113; the south followed in its winter). Each row
    // keeps the zonal wavenumbers a FILTER_U wind can advect stably in one
    // step, m < FILTER_CFL a cos(lat) / (FILTER_U dt), and loses the rest
    // over a short ramp -- the usual remedy on a latitude-longitude grid.
    // Keeping only what the grid resolves at 60 degrees was not enough.
    void polarFilter(std::vector<double>& q) {
        int M = W / 2 + 1;
        std::vector<double> R(M), I(M);
        for (int y = 0; y < H; y++) {
            if (!inChannel(y)) continue;
            double mmax = FILTER_CFL * A_EARTH * cosl[idx(0, y)] / (FILTER_U * DT);
            if (mmax >= W / 2) continue;
            for (int m = 0; m < M; m++) {
                double sr = 0, si = 0;
                for (int x = 0; x < W; x++) { double v = q[idx(x, y)]; sr += v * dftC[(size_t)m * W + x]; si -= v * dftS[(size_t)m * W + x]; }
                double sc = std::clamp((1.3 * mmax - m) / (0.3 * mmax), 0.0, 1.0);
                R[m] = sr / W * sc; I[m] = si / W * sc;
            }
            for (int x = 0; x < W; x++) {
                double v = 0;
                for (int m = 0; m < M; m++) {
                    double w = (m == 0 || m == W / 2) ? 1.0 : 2.0;
                    v += w * (R[m] * dftC[(size_t)m * W + x] - I[m] * dftS[(size_t)m * W + x]);
                }
                q[idx(x, y)] = v;
            }
        }
    }

    // one step, third-order strong-stability-preserving Runge-Kutta:
    // second order with centred differences amplifies grid-scale advection
    // by a factor that grows with the wind times the step, and blew up in
    // twelve hours; third order is stable for it.
    void step(double dt) {
        std::vector<double> s1(q1), s2(q2);
        invert(); tendency(k1a, k1b);
        for (int i = 0; i < W * H; i++) { q1[i] = s1[i] + dt * k1a[i]; q2[i] = s2[i] + dt * k1b[i]; }
        invert(); tendency(k2a, k2b);
        for (int i = 0; i < W * H; i++) {
            q1[i] = 0.75 * s1[i] + 0.25 * (q1[i] + dt * k2a[i]);
            q2[i] = 0.75 * s2[i] + 0.25 * (q2[i] + dt * k2b[i]);
        }
        invert(); tendency(k1a, k1b);
        for (int i = 0; i < W * H; i++) {
            q1[i] = (s1[i] + 2.0 * (q1[i] + dt * k1a[i])) / 3.0;
            q2[i] = (s2[i] + 2.0 * (q2[i] + dt * k1b[i])) / 3.0;
        }
        polarFilter(q1); polarFilter(q2);
        invert();
        winds();
    }

    void bank() {
        for (int y = 0; y < H; y++) {
            double mu = 0, mv = 0;
            for (int x = 0; x < W; x++) { mu += u2[idx(x, y)] / W; mv += v2[idx(x, y)] / W; }
            for (int x = 0; x < W; x++) {
                int i = idx(x, y);
                ekeAcc[i] += 0.5 * ((u2[i] - mu) * (u2[i] - mu) + (v2[i] - mv) * (v2[i] - mv));
                u2Acc[i] += u2[i]; v2Acc[i] += v2[i]; u1Acc[i] += u1[i]; psiAcc[i] += psi2[i];
            }
        }
        hoursBanked += 1;
    }
};

} // namespace qg2
