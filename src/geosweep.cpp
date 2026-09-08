// The geodesic atmosphere, scored against the same targets as the lat-lon one
// so the two can be compared directly.
//
//   cl /O2 /openmp /EHsc /std:c++17 src\geosweep.cpp /Fe:build\geosweep.exe
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
#include "terrain.h"
#include "hydrology.h"
#include "atmosphere.h"
#include "atmosphere_geo.h"

struct Target {
    const char* name;
    float lat;
    int season;
    float want;
    float weight;
};
static const Target TARGETS[] = {
    {"equator", 0, 2, 27, 1.0f},         {"subtropics", 25, 2, 30, 1.0f},
    {"mid-lat summer", 50, 2, 20, 1.5f}, {"mid-lat winter", 50, 0, -5, 1.5f},
    {"60N summer", 62, 2, 15, 1.5f},     {"60N winter", 62, 0, -25, 1.5f},
    {"polar summer", 82, 2, 0, 1.0f},    {"polar winter", 82, 0, -45, 1.0f},
};

int main(int argc, char** argv) {
    uint32_t seed = argc >= 2 ? (uint32_t)strtoul(argv[1], nullptr, 10) : 7;
    if (argc >= 3) atmosphere::SPINUP_DAYS = atoi(argv[2]) * 365;
    if (argc >= 4) atmosphere::KT_DIFF = atof(argv[3]);
    float landPct = 30.0f, conc = 50.0f;

    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> ang(0.0, 2 * 3.14159265358979), off(-2.0, 2.0);
    double a = ang(rng), b = ang(rng), cgl = ang(rng);
    double ca = cos(a), sa = sin(a), cb = cos(b), sb = sin(b), cc = cos(cgl), sc = sin(cgl);
    double mm[3][3] = {
        {ca * cb, ca * sb * sc - sa * cc, ca * sb * cc + sa * sc},
        {sa * cb, sa * sb * sc + ca * cc, sa * sb * cc - ca * sc},
        {-sb, cb * sc, cb * cc},
    };
    float rot[9];
    for (int col = 0; col < 3; col++)
        for (int row = 0; row < 3; row++) rot[col * 3 + row] = (float)mm[row][col];
    terrain::V3 offset = {(float)off(rng), (float)off(rng), (float)off(rng)};
    terrain::ContinentParams cp = terrain::paramsFor(conc / 100.0f);

    fprintf(stderr, "terrain...\n");
    plates::Field pf = plates::build(seed);
    float seaLevel = terrain::seaLevelFor(landPct / 100.0f, cp, rot, offset, pf);
    hydrology::Result hy = hydrology::build(cp, seaLevel, rot, offset, 12000.0f, pf);
    fprintf(stderr, "geodesic atmosphere, %d cells, spin-up %d days...\n",
            atmogeo::grid().size(), atmosphere::SPINUP_DAYS);
    atmosphere::Climatology c = atmogeo::build(cp, seaLevel, rot, offset, pf, hy, false);

    const int W = atmosphere::W, H = atmosphere::H, S = atmosphere::SEASONS;
    double gT = 0, gR = 0, gw = 0, gc = 0;
    for (int y = 0; y < H; y++) {
        double w = std::cos(((y + 0.5) / (double)H - 0.5) * 3.14159265);
        for (int x = 0; x < W; x++)
            for (int se = 0; se < S; se++) {
                int i = se * W * H + y * W + x;
                gT += c.meanT[i] * w;
                gR += c.rainMmDay[i] * w;
                gc += c.cloud[i] * w;
                gw += w;
            }
    }
    double dry = 0, land = 0, cr = 0, cn = 0, ir = 0, in_ = 0;
    for (int i = 0; i < W * H; i++) {
        if (c.elev[i] <= 0.0f) continue;
        double r = 0;
        for (int se = 0; se < S; se++) r += c.rainMmDay[se * W * H + i] / S;
        land++;
        if (r < 0.5) dry++;
        int x = i % W, y = i / W;
        bool coastal = false;
        for (int dy = -2; dy <= 2 && !coastal; dy++)
            for (int dx = -2; dx <= 2 && !coastal; dx++) {
                int yy = y + dy;
                if (yy < 0 || yy >= H) continue;
                int xx = ((x + dx) % W + W) % W;
                if (c.elev[yy * W + xx] <= 0.0f) coastal = true;
            }
        if (coastal) { cr += r; cn++; } else { ir += r; in_++; }
    }

    double err = 0, spot[8];
    int k = 0;
    double polarRain = 0;
    for (const Target& t : TARGETS) {
        int y = (int)((t.lat / 180.0f + 0.5f) * H);
        y = y < 0 ? 0 : (y >= H ? H - 1 : y);
        double sum = 0, rn = 0;
        for (int x = 0; x < W; x++) {
            sum += c.meanT[t.season * W * H + y * W + x];
            rn += c.rainMmDay[t.season * W * H + y * W + x];
        }
        spot[k++] = sum / W;
        double d = (sum / W - t.want) / 10.0;
        err += t.weight * d * d;
        if (t.lat > 80) polarRain = std::max(polarRain, rn / W);
    }
    double mean = gT / gw, rain = gR / gw;
    double dm = (mean - 15.0) / 5.0, dr = (rain - 2.7) / 1.0;
    err += 3.0 * dm * dm + 2.0 * dr * dr;
    double dp = std::max(polarRain - 0.5, 0.0);
    err += 2.0 * dp * dp;
    double dd = (dry / std::max(land, 1.0) - 0.30) / 0.15;
    err += 3.0 * dd * dd;

    fprintf(stderr,
            "GEODESIC | err %6.1f | mean %5.1f rain %4.2f pRain %4.1f dry %3.0f%% cloud %3.0f%%\n"
            "  eq %5.1f sub %5.1f mls %5.1f mlw %5.1f 60s %5.1f 60w %5.1f ps %5.1f pw %5.1f\n"
            "  want   27      30       20      -5      15     -25       0     -45\n"
            "  coast %4.2f inland %4.2f\n",
            err, mean, rain, polarRain, 100 * dry / std::max(land, 1.0), 100 * gc / gw, spot[0],
            spot[1], spot[2], spot[3], spot[4], spot[5], spot[6], spot[7],
            cn > 0 ? cr / cn : 0.0, in_ > 0 ? ir / in_ : 0.0);

    // Zonal profile, and the wind, since this is what the whole exercise was
    // about: nothing special is supposed to happen at the poles now.
    fprintf(stderr, "\n%6s %7s %7s %7s %7s %7s\n", "lat", "DJF", "JJA", "Tair", "rain", "|u|");
    for (int y = H - 2; y >= 1; y -= 6) {
        double la = ((y + 0.5) / (double)H - 0.5) * 180.0;
        double w = 0, s = 0, ta = 0, rn = 0, sp = 0;
        for (int x = 0; x < W; x++) {
            w += c.meanT[0 * W * H + y * W + x];
            s += c.meanT[2 * W * H + y * W + x];
            ta += c.airT[2 * W * H + y * W + x];
            for (int se = 0; se < S; se++) rn += c.rainMmDay[se * W * H + y * W + x] / S;
            double u = c.windU[2 * W * H + y * W + x], v = c.windV[2 * W * H + y * W + x];
            sp += std::sqrt(u * u + v * v);
        }
        fprintf(stderr, "%6.0f %7.1f %7.1f %7.1f %7.2f %7.1f\n", la, w / W, s / W, ta / W, rn / W,
                sp / W);
    }
    return 0;
}
