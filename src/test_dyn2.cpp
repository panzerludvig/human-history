// Standalone test of the two-level dynamics: a flat planet with a zonal
// temperature profile, no polar filter, and the state's extremes printed
// as it runs. If this is not stable, nothing built on it will be.
#include "dynamics2.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    const int W = 192, H = 96;
    int steps = argc >= 2 ? atoi(argv[1]) : 720;
    int printEvery = argc >= 3 ? atoi(argv[2]) : 30;
    double blobK = argc >= 4 ? atof(argv[3]) : 0.0;      // a Siberian cold blob at 65N, 100E, 800 km
    dyn2::EXCHANGE = argc >= 5 ? atof(argv[4]) : 1.0;
    bool balanced = argc >= 6 && atoi(argv[5]) != 0;
    dyn2::Model m;
    std::vector<float> elev(W * H, 0.0f), lat(W * H, 0.0f);
    std::vector<unsigned char> water(W * H, 1);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) lat[y * W + x] = (float)((((y + 0.5) / H) - 0.5) * 3.14159265);
    m.init(W, H, elev, lat, water);
    std::vector<double> tns(W * H);
    for (int i = 0; i < W * H; i++) {
        double a = std::fabs(lat[i]) * 180.0 / 3.14159265;
        tns[i] = 27.0 - 55.0 * std::pow(a / 90.0, 1.5); // 27 at the equator, -28 at the pole
        double lonD = ((i % W) + 0.5) / W * 360.0 - 180.0, latD = lat[i] * 180.0 / 3.14159265;
        double dxk = (lonD - 100.0) * 111.0 * std::cos(65.0 * 3.14159265 / 180.0), dyk = (latD - 65.0) * 111.0;
        tns[i] -= blobK * std::exp(-(dxk * dxk + dyk * dyk) / (800.0 * 800.0));
    }
    m.setTargets(tns, true);
    if (balanced) m.balancePs();
    // a seed for the eddies: a hectopascal of noise
    // wavenumber five, two kelvin, in the lower level's theta
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) m.th1[y * W + x] += 2.0 * std::sin(5.0 * 2 * 3.14159265 * (x + 0.5) / W);
    m.nth1 = m.th1;
    for (int s = 0; s <= steps; s++) {
        if (s % printEvery == 0) {
            double pmin = 1e9, pmax = -1e9, umax = 0, u2max = 0; int iu = 0;
            for (int i = 0; i < W * H; i++) {
                pmin = std::min(pmin, m.ps[i]); pmax = std::max(pmax, m.ps[i]);
                double sp = std::fabs(m.u1[i]) + std::fabs(m.v1[i]);
                if (sp > umax) { umax = sp; iu = i; }
                u2max = std::max(u2max, std::fabs(m.u2[i]) + std::fabs(m.v2[i]));
            }
            // eddy kinetic energy of the lower level against the zonal mean, 30-60 deg
            double eke = 0; int ne = 0;
            for (int y = 0; y < H; y++) {
                double la = std::fabs(((y + 0.5) / H - 0.5) * 180.0);
                if (la < 30 || la > 60) continue;
                double mu = 0, mv = 0;
                for (int x = 0; x < W; x++) { mu += m.u1[y * W + x] / W; mv += m.v1[y * W + x] / W; }
                for (int x = 0; x < W; x++) { int i = y * W + x; eke += 0.5 * ((m.u1[i] - mu) * (m.u1[i] - mu) + (m.v1[i] - mv) * (m.v1[i] - mv)); ne++; }
            }
            printf("day %5.1f  ps [%7.1f, %7.1f] hPa  |u1| max %6.2f at lat %5.1f  |u2| max %6.2f  EKE(30-60) %7.2f\n",
                   s * dyn2::DT / 86400.0, pmin / 100, pmax / 100, umax, (((iu / W) + 0.5) / H - 0.5) * 180.0, u2max, eke / std::max(ne, 1));
        }
        m.refreshExner();
        m.step(dyn2::DT, nullptr, nullptr);
    }
    // the zonal mean wind at the end
    printf("zonal mean u1 / u2 by latitude:\n");
    for (int y = 4; y < H; y += 8) {
        double u1 = 0, u2 = 0;
        for (int x = 0; x < W; x++) { u1 += m.u1[y * W + x] / W; u2 += m.u2[y * W + x] / W; }
        printf("  %5.0f %7.2f %7.2f\n", ((y + 0.5) / H - 0.5) * 180.0, u1, u2);
    }
    return 0;
}
