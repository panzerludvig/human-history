// Does the mesh elliptic solver work, and is it fast enough to invert
// potential vorticity six times a step for a year? Checked three ways:
// the discrete round trip (lap of a field, solved back: only the solver's
// tolerance should remain), the analytic right-hand side (spherical
// harmonics, whose Laplacian is -l(l+1)/R^2 times themselves: what remains
// is the discretisation error), and the Helmholtz case at the QG model's
// coupling. Then the cost, cold and warm.
//
//   build_solvetest.bat
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include "geodesic.h"
#include "geosolve.h"

using namespace geodesic;
static const double R = 6371000.0;

static double rms(const std::vector<double>& x) { double s = 0; for (double v : x) s += v * v; return std::sqrt(s / x.size()); }
static double rmsDiff(const std::vector<double>& x, const std::vector<double>& y) { double s = 0; for (size_t i = 0; i < x.size(); i++) s += (x[i] - y[i]) * (x[i] - y[i]); return std::sqrt(s / x.size()); }
static double now() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

int main(int argc, char** argv) {
    int level = argc >= 2 ? atoi(argv[1]) : 5;
    Grid g = build(level);
    int N = g.size();
    printf("level %d, %d cells\n\n", level, N);
    int drop = argc >= 3 ? atoi(argv[2]) : 3;
    Solver S; S.init(g, R, drop);
    S.twoLevel = drop > 0;
    printf("preconditioner: %s, %d aggregates\n", S.twoLevel ? "Jacobi + coarse level" : "Jacobi", S.twoLevel ? S.Nc : 0);

    // a field of degrees 1, 2 and 3: z, xy, x(5z^2 - 1); eigenvalues -2, -6, -12 over R^2
    std::vector<double> f(N), rhsA(N), rhsD(N), psi(N, 0.0);
    for (int i = 0; i < N; i++) {
        double x = g.c[i].x, y = g.c[i].y, z = g.c[i].z;
        double f1 = z, f2 = x * y, f3 = x * (5 * z * z - 1);
        f[i] = 1e7 * (f1 + 0.5 * f2 + 0.3 * f3);
        rhsA[i] = -1e7 * (2 * f1 + 6 * 0.5 * f2 + 12 * 0.3 * f3) / (R * R);
    }
    for (int i = 0; i < N; i++) rhsD[i] = lap(g, f, i, R);
    double fm = S.areaMean(f); for (int i = 0; i < N; i++) f[i] -= fm;
    printf("field rms %.4g m2/s\n", rms(f));

    // 1. discrete round trip, Poisson
    std::fill(psi.begin(), psi.end(), 0.0);
    double t0 = now(); int it = S.solve(rhsD, 0.0, psi, 1e-9); double t1 = now();
    printf("Poisson, discrete rhs, cold:   %4d iterations %7.1f ms  rel error %.2e\n", it, t1 - t0, rmsDiff(psi, f) / rms(f));
    // 2. analytic rhs: the discretisation error
    std::fill(psi.begin(), psi.end(), 0.0);
    t0 = now(); it = S.solve(rhsA, 0.0, psi, 1e-9); t1 = now();
    printf("Poisson, analytic rhs, cold:   %4d iterations %7.1f ms  rel error %.2e  (discretisation)\n", it, t1 - t0, rmsDiff(psi, f) / rms(f));
    // 3. Helmholtz at the QG coupling, 2F with F = 2.1e-12 m^-2
    double kappa = 4.2e-12;
    std::vector<double> rhsH(N);
    for (int i = 0; i < N; i++) rhsH[i] = rhsD[i] - kappa * f[i];
    std::fill(psi.begin(), psi.end(), 0.0);
    t0 = now(); it = S.solve(rhsH, kappa, psi, 1e-9); t1 = now();
    printf("Helmholtz, discrete rhs, cold: %4d iterations %7.1f ms  rel error %.2e\n", it, t1 - t0, rmsDiff(psi, f) / rms(f));
    // 4. warm start: the field moved by one percent, as between stages
    std::vector<double> f2(N), rhs2(N);
    for (int i = 0; i < N; i++) f2[i] = f[i] * 1.01 + 1e5 * g.c[i].y * g.c[i].z;
    for (int i = 0; i < N; i++) rhs2[i] = lap(g, f2, i, R);
    psi = f;
    t0 = now(); it = S.solve(rhs2, 0.0, psi, 1e-7); t1 = now();
    double fm2 = S.areaMean(f2); for (int i = 0; i < N; i++) f2[i] -= fm2;
    printf("Poisson, warm start (1%% move): %4d iterations %7.1f ms  rel error %.2e  at tol 1e-7\n", it, t1 - t0, rmsDiff(psi, f2) / rms(f2));
    for (int i = 0; i < N; i++) rhs2[i] = lap(g, f2, i, R) - kappa * f2[i];
    psi = f;
    t0 = now(); it = S.solve(rhs2, kappa, psi, 1e-7); t1 = now();
    printf("Helmholtz, warm start:         %4d iterations %7.1f ms  rel error %.2e  at tol 1e-7\n", it, t1 - t0, rmsDiff(psi, f2) / rms(f2));
    double warmH = t1 - t0;
    for (double tl : {1e-5, 1e-4}) {
        psi = f; for (int i = 0; i < N; i++) rhs2[i] = lap(g, f2, i, R);
        t0 = now(); it = S.solve(rhs2, 0.0, psi, tl); t1 = now();
        printf("Poisson, warm start, tol %.0e: %4d iterations %7.1f ms  rel error %.2e\n", tl, it, t1 - t0, rmsDiff(psi, f2) / rms(f2));
    }
    {   // extrapolated start: the field moves once more by the same amount, and the
        // guess is that move again, wrong by one percent of it
        std::vector<double> f3(N);
        for (int i = 0; i < N; i++) f3[i] = 2 * f2[i] - f[i];
        for (int i = 0; i < N; i++) rhs2[i] = lap(g, f3, i, R);
        for (int i = 0; i < N; i++) psi[i] = f3[i] + 0.01 * (f2[i] - f[i]) * g.c[i].x;
        t0 = now(); it = S.solve(rhs2, 0.0, psi, 1e-5); t1 = now();
        double fm3 = S.areaMean(f3); for (int i = 0; i < N; i++) f3[i] -= fm3;
        printf("Poisson, extrapolated start, tol 1e-5: %4d iterations %7.1f ms  rel error %.2e\n", it, t1 - t0, rmsDiff(psi, f3) / rms(f3));
    }
    psi = f; for (int i = 0; i < N; i++) rhs2[i] = lap(g, f2, i, R);
    t0 = now(); S.solve(rhs2, 0.0, psi, 1e-7); double warmP = now() - t0;
    double perStep = 3 * (warmP + warmH);
    printf("\nbudget: a year of half-hour steps, three stages, a Poisson and a Helmholtz each, at the warm cost: %.0f s\n", perStep * 48 * 365 / 1000.0);
    return 0;
}
