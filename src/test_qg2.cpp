// Standalone test of the two-layer QG model: a zonal temperature profile
// with an optional cold blob, a seed, the extremes and the eddy energy
// printed as it runs, and the zonal-mean winds at the end.
#include "qg2.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    const int W = 192, H = 96;
    int days = argc >= 2 ? atoi(argv[1]) : 30;
    int printEvery = argc >= 3 ? atoi(argv[2]) : 5;
    double blobK = argc >= 4 ? atof(argv[3]) : 0.0;
    qg2::Model m;
    std::vector<float> elev(W * H, 0.0f), lat(W * H, 0.0f);
    std::vector<unsigned char> water(W * H, 1);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) lat[y * W + x] = (float)((((y + 0.5) / H) - 0.5) * 3.14159265);
    m.init(W, H, elev, lat, water);
    std::vector<double> tns(W * H);
    for (int i = 0; i < W * H; i++) {
        double a = std::fabs(lat[i]) * 180.0 / 3.14159265;
        tns[i] = 27.0 - 55.0 * std::pow(a / 90.0, 1.5);
        double lonD = ((i % W) + 0.5) / W * 360.0 - 180.0, latD = lat[i] * 180.0 / 3.14159265;
        double dxk = (lonD - 100.0) * 111.0 * std::cos(65.0 * 3.14159265 / 180.0), dyk = (latD - 65.0) * 111.0;
        tns[i] -= blobK * std::exp(-(dxk * dxk + dyk * dyk) / (800.0 * 800.0));
    }
    m.setTargets(tns, true);
    // seed: a little vorticity noise in the lower layer
    srand(7);
    for (int y = 0; y < H; y++)
        if (m.inChannel(y))
            for (int x = 0; x < W; x++) m.q2[y * W + x] += 1e-5 * std::sin(5.0 * 2 * 3.14159265 * (x + 0.5) / W + 0.3 * y);
    int stepsPerDay = (int)(86400.0 / qg2::DT);
    for (int d = 0; d <= days; d++) {
        if (d % printEvery == 0) {
            double umax = 0, u2max = 0, eke = 0; int ne = 0, iu = 0;
            for (int y = 0; y < H; y++) {
                if (!m.inChannel(y)) continue;
                double mu = 0, mv = 0;
                for (int x = 0; x < W; x++) { mu += m.u2[y * W + x] / W; mv += m.v2[y * W + x] / W; }
                for (int x = 0; x < W; x++) {
                    int i = y * W + x;
                    double sp = std::fabs(m.u1[i]) + std::fabs(m.v1[i]);
                    if (sp > umax) { umax = sp; iu = i; }
                    u2max = std::max(u2max, std::fabs(m.u2[i]) + std::fabs(m.v2[i]));
                    double la = std::fabs(((y + 0.5) / H - 0.5) * 180.0);
                    if (la >= 30 && la <= 60) { eke += 0.5 * ((m.u2[i] - mu) * (m.u2[i] - mu) + (m.v2[i] - mv) * (m.v2[i] - mv)); ne++; }
                }
            }
            printf("day %4d  |u upper| max %6.2f at lat %5.1f  |u lower| max %6.2f  EKE lower 30-60 %8.3f\n",
                   d, umax, (((iu / W) + 0.5) / H - 0.5) * 180.0, u2max, eke / std::max(ne, 1));
        }
        for (int s = 0; s < stepsPerDay; s++) m.step(qg2::DT);
    }
    printf("zonal mean upper / lower u by latitude:\n");
    for (int y = 6; y < H; y += 6) {
        if (!m.inChannel(y)) continue;
        double a = 0, b = 0;
        for (int x = 0; x < W; x++) { a += m.u1[y * W + x] / W; b += m.u2[y * W + x] / W; }
        printf("  %5.0f %7.2f %7.2f\n", ((y + 0.5) / H - 0.5) * 180.0, a, b);
    }
    return 0;
}
