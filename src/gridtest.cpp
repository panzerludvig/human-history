// Does the geodesic grid actually work? Every operator checked against a case
// whose answer is known in closed form, because a discretisation you have not
// verified is worse than the one you are replacing -- it fails silently, and
// the failure looks like weather.
//
//   cl /O2 /EHsc /std:c++17 src\gridtest.cpp /Fe:build\gridtest.exe
#include <cstdio>
#include "geodesic.h"

using namespace geodesic;

static const double R = 6371000.0;
static const double PI = 3.14159265358979323846;

static int failures = 0;
static void check(const char* what, double got, double want, double tol, const char* unit = "") {
    double err = std::fabs(got - want);
    bool ok = err <= tol;
    if (!ok) failures++;
    printf("  %-46s %12.6g  want %10.6g  %s%s\n", what, got, want, unit, ok ? "" : "   <-- FAIL");
}

int main(int argc, char** argv) {
    int level = argc >= 2 ? atoi(argv[1]) : 5;
    printf("Geodesic grid, subdivision level %d\n\n", level);
    int passes = argc >= 3 ? atoi(argv[2]) : geodesic::RELAX_PASSES;
    Grid g = build(level, passes);
    int N = g.size();
    printf("relaxation: %d Lloyd passes\n\n", g.relaxPasses);

    // ---------------------------------------------------------- topology
    printf("TOPOLOGY\n");
    check("cell count", N, 10.0 * std::pow(4, level) + 2, 0);
    int pent = 0, hex = 0, other = 0;
    for (int i = 0; i < N; i++) {
        int d = g.deg(i);
        if (d == 5) pent++;
        else if (d == 6) hex++;
        else other++;
    }
    check("pentagons", pent, 12, 0);
    check("cells that are not 5- or 6-sided", other, 0, 0);
    printf("  %-46s %12d\n", "hexagons", hex);

    // every neighbour relation must run both ways
    int asym = 0;
    for (int i = 0; i < N; i++)
        for (int k = g.nbrStart[i]; k < g.nbrStart[i + 1]; k++) {
            int j = g.nbr[k];
            bool back = false;
            for (int m = g.nbrStart[j]; m < g.nbrStart[j + 1]; m++)
                if (g.nbr[m] == i) back = true;
            if (!back) asym++;
        }
    check("one-way neighbour links", asym, 0, 0);

    // ---------------------------------------------------------- geometry
    printf("\nGEOMETRY\n");
    double A = 0;
    for (double a : g.area) A += a;
    check("total area / 4pi", A / (4 * PI), 1.0, 1e-9);

    // A cell's outward normals weighted by edge length must close on zero,
    // or the gradient operator has a constant bias: grad(constant) != 0.
    double worstClose = 0;
    for (int i = 0; i < N; i++) {
        D3 s{0, 0, 0};
        for (int k = g.nbrStart[i]; k < g.nbrStart[i + 1]; k++)
            s = s + g.enorm[k] * g.elen[k];
        worstClose = std::max(worstClose, len(s) / g.area[i]);
    }
    check("worst cell closure |sum L*n| / A", worstClose, 0.0, 2e-2);

    double lo = minSpacing(g, R), hi = maxSpacing(g, R);
    printf("  %-46s %9.1f km\n", "shortest edge", lo / 1000);
    printf("  %-46s %9.1f km\n", "longest edge", hi / 1000);
    check("spacing ratio longest/shortest", hi / lo, 1.0, 0.35);
    double amin = 1e30, amax = 0;
    for (double a : g.area) { amin = std::min(amin, a); amax = std::max(amax, a); }
    // Measured, converged, and NOT what I predicted. Unrelaxed this is 1.36 and
    // I expected Lloyd to take it towards 1.1; it converges to 1.41, slightly
    // worse. Lloyd optimises COMPACTNESS -- how round each cell is -- and not
    // equal area, and on a sphere carrying twelve forced pentagons those two
    // goals pull in different directions. The operators are what improved, by
    // up to eighteen times, which was the real reason for doing it.
    //
    // It also grows with resolution (1.33, 1.41, 1.57 at levels 4, 5, 6)
    // because the long-wavelength modes settle more slowly on a bigger mesh.
    // Against 61:1 on the grid this replaces, none of it limits anything.
    check("area ratio largest/smallest", amax / amin, 1.0, 0.60);

    // What a lat-lon grid of similar size does, for contrast.
    {
        int W = 192, H = 96;
        double dyLL = PI * R / H;
        double dxEq = 2 * PI * R / W;
        double dxPole = dxEq * std::cos((0.5 / H - 0.5) * PI + PI); // top row
        dxPole = dxEq * std::cos(((H - 0.5) / H - 0.5) * PI);
        printf("\n  for contrast, 192x96 lat-lon: %d cells, dx %.1f km at the equator,\n"
               "  %.2f km in the top row -- a ratio of %.0f to 1\n",
               W * H, dxEq / 1000, std::fabs(dxPole) / 1000, dxEq / std::fabs(dxPole));
    }

    // Would triangles have been more uniform? They are already here -- these
    // cells are the DUAL of a triangulation -- so the question can be answered
    // by measuring rather than argued about.
    {
        int F = (int)g.tri.size() / 3;
        double tmin = 1e30, tmax = 0, emin = 1e30, emax = 0;
        for (int f = 0; f < F; f++) {
            D3 a3 = g.c[g.tri[3 * f]], b3 = g.c[g.tri[3 * f + 1]], c3 = g.c[g.tri[3 * f + 2]];
            double ar = triArea(a3, b3, c3);
            tmin = std::min(tmin, ar);
            tmax = std::max(tmax, ar);
            double e1 = std::acos(std::clamp(dot(a3, b3), -1.0, 1.0));
            double e2 = std::acos(std::clamp(dot(b3, c3), -1.0, 1.0));
            double e3 = std::acos(std::clamp(dot(c3, a3), -1.0, 1.0));
            emin = std::min({emin, e1, e2, e3});
            emax = std::max({emax, e1, e2, e3});
        }
        printf("\nTRIANGLES vs THEIR DUAL HEXAGONS\n");
        printf("  %-46s %8d\n", "triangles", F);
        printf("  %-46s %12.4f\n", "triangle area ratio largest/smallest", tmax / tmin);
        printf("  %-46s %12.4f\n", "hexagon  area ratio largest/smallest", amax / amin);
        printf("  %-46s %12.4f\n", "triangle edge ratio longest/shortest", emax / emin);
        printf("  %-46s %8d vs %d\n", "neighbours per cell (tri vs hex)", 3, 6);
    }

    // ---------------------------------------------------------- operators
    printf("\nOPERATORS, against closed form\n");

    // f = z is the l=1 harmonic. Its surface gradient is the tangent part of
    // z_hat over R, and its Laplacian is -l(l+1) f / R^2 = -2z/R^2.
    std::vector<double> f(N);
    for (int i = 0; i < N; i++) f[i] = g.c[i].z;

    double gErr = 0, gMag = 0, lErr = 0, lMag = 0;
    for (int i = 0; i < N; i++) {
        D3 want = tangent({0, 0, 1}, g.c[i]) * (1.0 / R);
        D3 got = grad(g, f, i, R);
        gErr = std::max(gErr, len(got - want));
        gMag = std::max(gMag, len(want));
        double lw = -2.0 * g.c[i].z / (R * R);
        double lg = lap(g, f, i, R);
        lErr = std::max(lErr, std::fabs(lg - lw));
        lMag = std::max(lMag, std::fabs(lw));
    }
    check("gradient of z: worst relative error", gErr / gMag, 0.0, 0.08);
    // The worst case is not the story if it happens at twelve cells out of ten
    // thousand. Where the error lives decides whether it matters.
    {
        double rms = 0, pentWorst = 0, hexWorst = 0;
        for (int i = 0; i < N; i++) {
            D3 want = tangent({0, 0, 1}, g.c[i]) * (1.0 / R);
            double e = len(grad(g, f, i, R) - want) / gMag;
            rms += e * e;
            if (g.deg(i) == 5) pentWorst = std::max(pentWorst, e);
            else hexWorst = std::max(hexWorst, e);
        }
        check("gradient of z: RMS relative error", std::sqrt(rms / N), 0.0, 0.02);
        printf("  %-46s %12.4f\n", "  ... worst at the 12 pentagons", pentWorst);
        printf("  %-46s %12.4f\n", "  ... worst anywhere else", hexWorst);
        int over = 0;
        for (int i = 0; i < N; i++) {
            D3 want = tangent({0, 0, 1}, g.c[i]) * (1.0 / R);
            if (len(grad(g, f, i, R) - want) / gMag > 0.02) over++;
        }
        printf("  %-46s %8d of %d\n", "  ... cells worse than 2%", over, N);
    }
    check("laplacian of z: worst relative error", lErr / lMag, 0.0, 0.05);

    // A constant field must have zero gradient -- this is the closure test
    // again, seen from the other side.
    std::vector<double> ones(N, 1.0);
    double cErr = 0;
    for (int i = 0; i < N; i++) cErr = std::max(cErr, len(grad(g, ones, i, R)) * R);
    check("gradient of a constant", cErr, 0.0, 2e-2);

    // Solid-body rotation is tangent everywhere and perfectly non-divergent.
    std::vector<D3> V(N);
    D3 axis = norm(D3{0.3, 0.4, 1.0});
    for (int i = 0; i < N; i++) V[i] = cross(axis, g.c[i]) * (R * 1e-5);
    double dErr = 0, dScale = 0;
    for (int i = 0; i < N; i++) {
        dErr = std::max(dErr, std::fabs(div(g, V, i, R)));
        dScale = std::max(dScale, len(V[i]) / R);
    }
    check("divergence of solid-body rotation", dErr / dScale, 0.0, 0.02);

    // Advecting a constant by any flow must not change it.
    double aErr = 0;
    for (int i = 0; i < N; i++) aErr = std::max(aErr, std::fabs(advect(g, ones, V, i, R)));
    check("advection of a constant", aErr / dScale, 0.0, 0.02);

    // ------------------------------------------------------------- timestep
    printf("\nWHAT THIS BUYS\n");
    double c = std::sqrt(9.81 * 11000.0);
    printf("  %-46s %9.1f m/s\n", "external gravity wave speed", c);
    printf("  %-46s %9.1f s\n", "stable step here (CFL=1 at shortest edge)", lo / c);
    {
        double dxPole = (2 * PI * R / 192) * std::cos(((95.5) / 96 - 0.5) * PI);
        printf("  %-46s %9.1f s   <- the grid we are leaving\n", "stable step on lat-lon at 87 deg",
               std::fabs(dxPole) / c);
    }

    printf("\n%s\n", failures == 0 ? "all checks passed" : "SOME CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
