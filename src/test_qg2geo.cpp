// The two-layer QG model on the geodesic grid, standalone: a zonal
// temperature profile, a seed, the extremes and the eddy energy as it
// runs, the zonal-mean winds at the end, and the cost.
//
//   test_qg2geo.exe [days] [printEvery]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include "qg2geo.h"

using namespace qg2geo;
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

int main(int argc, char** argv) {
    int days = argc >= 2 ? atoi(argv[1]) : 30;
    int printEvery = argc >= 3 ? atoi(argv[2]) : 5;
    Model m;
    m.init({}, {});
    int N = m.N;
    printf("%d cells, flux sign %+.0f, F %.3g\n", N, m.fluxSign, m.F);

    // the wind from a solid-body rotation streamfunction, against U cos(lat)
    {
        std::vector<double> ps(N);
        for (int i = 0; i < N; i++) ps[i] = -10.0 * A_EARTH * m.g.c[i].z;
        double worst = 0;
        for (int i = 0; i < N; i++) {
            geodesic::D3 V = geodesic::cross(m.g.c[i], geodesic::grad(m.g, ps, i, A_EARTH));
            double u = geodesic::dot(V, m.east[i]), v = geodesic::dot(V, m.north[i]);
            worst = std::max(worst, std::fabs(u - 10.0 * std::cos(m.lat[i])) + std::fabs(v));
        }
        printf("solid-body wind from psi: worst error %.3f m/s of 10\n", worst);
        // and the corner flux: nondivergent to rounding?
        double worstDiv = 0;
        for (int i = 0; i < N; i++) {
            double s = 0;
            for (int k = 0; k < m.g.deg(i); k++) s += m.cornerFlux(ps, i, k);
            worstDiv = std::max(worstDiv, std::fabs(s));
        }
        printf("corner flux divergence: worst %.3g m2/s (fluxes are ~%.3g)\n", worstDiv, 10.0 * A_EARTH * 0.04);
    }

    std::vector<double> tns(N);
    for (int i = 0; i < N; i++) {
        double a = std::fabs(m.lat[i]) * 180.0 / PI;
        tns[i] = 27.0 - 55.0 * std::pow(a / 90.0, 1.5);
    }
    m.setTargets(tns, true);
    for (int i = 0; i < N; i++) m.q2[i] += 1e-5 * std::sin(5.0 * m.lon[i] + 0.3 * m.lat[i] * 180.0 / PI);

    int stepsPerDay = (int)(86400.0 / DT);
    double t0 = now(); long iters = 0; int nSolves = 0;
    for (int d = 0; d <= days; d++) {
        if (d % printEvery == 0) {
            double umax = 0, u2max = 0, eke = 0, ekeA = 0; int ne = 0, iu = 0; bool fin = true;
            m.zonalMean(m.u2, m.k2a); m.zonalMean(m.v2, m.k2b);
            for (int i = 0; i < N; i++) {
                if (!std::isfinite(m.u1[i]) || !std::isfinite(m.u2[i])) fin = false;
                double sp = geodesic::len(m.V1[i]);
                if (sp > umax) { umax = sp; iu = i; }
                u2max = std::max(u2max, geodesic::len(m.V2[i]));
                double la = std::fabs(m.lat[i]) * 180.0 / PI;
                double e = 0.5 * ((m.u2[i] - m.k2a[i]) * (m.u2[i] - m.k2a[i]) + (m.v2[i] - m.k2b[i]) * (m.v2[i] - m.k2b[i]));
                if (la >= 30 && la <= 60) { eke += e * m.g.area[i]; ekeA += m.g.area[i]; }
            }
            printf("day %4d  |V upper| max %6.2f at lat %5.1f  |V lower| max %6.2f  EKE lower 30-60 %8.3f  %s\n",
                   d, umax, m.lat[iu] * 180.0 / PI, u2max, eke / std::max(ekeA, 1e-30), fin ? "" : "NON-FINITE");
            if (!fin) return 1;
        }
        for (int s = 0; s < stepsPerDay; s++) { m.step(DT); iters += m.solveIterations; nSolves += 2; }
    }
    double el = now() - t0;
    printf("\n%.1f s for %d days: %.2f s/day, %.1f min/year; %.1f solver iterations per inversion\n",
           el, days, el / std::max(days, 1), el / std::max(days, 1) * 365 / 60, (double)iters / std::max(nSolves, 1));
    printf("zonal mean upper / lower u by latitude:\n");
    std::vector<double> zu1(N), zu2(N);
    m.zonalMean(m.u1, zu1); m.zonalMean(m.u2, zu2);
    for (int b = 0; b < m.nBands; b += 5) {
        double la = -90 + (b + 0.5) * ZONAL_BAND;
        int any = -1;
        for (int i = 0; i < N; i++) if (m.band[i] == b) { any = i; break; }
        if (any >= 0) printf("  %5.0f %7.2f %7.2f\n", la, zu1[any], zu2[any]);
    }
    return 0;
}
