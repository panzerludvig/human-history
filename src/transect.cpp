// A transect across a coast. Builds one world and walks a line of cells from
// the open sea into the middle of a continent, printing every quantity in the
// chain -- pressure, wind, water in the air, how near saturation it is, what
// falls, and what the surface is doing -- against what each should be.
//
// Averages over the whole tropics-to-pole picture hide the thing that is
// actually wrong: a world can have the right amount of rain and put all of it
// in the wrong place.
//
//   cl /O2 /openmp /EHsc /std:c++17 src\transect.cpp /Fe:build\transect.exe
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>
#include "terrain.h"
#include "hydrology.h"
#include "atmosphere.h"

int main(int argc, char** argv) {
    uint32_t seed = argc >= 2 ? (uint32_t)strtoul(argv[1], nullptr, 10) : 7;
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

    fprintf(stderr, "building...\n");
    plates::Field pf = plates::build(seed);
    float seaLevel = terrain::seaLevelFor(landPct / 100.0f, cp, rot, offset, pf);
    hydrology::Result hy = hydrology::build(cp, seaLevel, rot, offset, 12000.0f, pf);
    atmosphere::Climatology c = atmosphere::build(cp, seaLevel, rot, offset, pf, hy, false);

    const int W = atmosphere::W, H = atmosphere::H, S = atmosphere::SEASONS;
    auto at = [&](const std::vector<float>& v, int se, int x, int y) {
        return v[se * W * H + y * W + ((x % W) + W) % W];
    };

    // Find the widest run of land on some mid-latitude row: that is a
    // continent worth crossing.
    int bestY = -1, bestX = -1, bestRun = 0;
    for (int y = 0; y < H; y++) {
        double lat = ((y + 0.5) / H - 0.5) * 180.0;
        if (std::fabs(lat) < 25 || std::fabs(lat) > 55) continue;
        int run = 0, start = 0;
        for (int x = 0; x < W * 2; x++) {
            if (c.elev[(x % W) + y * W] > 0) {
                if (run == 0) start = x;
                run++;
                if (run > bestRun) { bestRun = run; bestY = y; bestX = start; }
            } else run = 0;
        }
    }
    if (bestY < 0) { fprintf(stderr, "no continent found\n"); return 1; }
    double lat = ((bestY + 0.5) / H - 0.5) * 180.0;
    int summer = lat >= 0 ? 2 : 0, winter = lat >= 0 ? 0 : 2;
    fprintf(stderr,
            "\nTransect at %.1f deg, %d cells of land (~%.0f km), from %d cells offshore.\n"
            "Summer = season %d. Cell is ~%.0f km wide here.\n\n",
            lat, bestRun, bestRun * 2 * 3.14159265 * 6371.0 / W * std::cos(lat * 3.14159 / 180),
            4, summer, 2 * 3.14159265 * 6371.0 / W * std::cos(lat * 3.14159 / 180));

    printf("%4s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s\n", "cell", "elev", "T_sum", "T_win",
           "Tair", "press", "u", "v", "water", "RH", "rain", "cloud");
    printf("%4s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s\n", "", "m", "C", "C", "C", "m", "m/s",
           "m/s", "mm", "%", "mm/d", "%");
    for (int k = -4; k < bestRun + 1; k++) {
        int x = bestX + k;
        int y = bestY;
        double rainS = at(c.rainMmDay, summer, x, y), rainW = at(c.rainMmDay, winter, x, y);
        printf("%4d %6.0f %6.1f %6.1f %6.1f %6.0f %6.1f %6.1f %6.1f %6.0f %6.2f %6.0f%s\n", k,
               c.elev[((x % W) + W) % W + y * W], at(c.meanT, summer, x, y),
               at(c.meanT, winter, x, y), at(c.airT, summer, x, y), at(c.press, summer, x, y),
               at(c.windU, summer, x, y), at(c.windV, summer, x, y), at(c.wv, summer, x, y),
               at(c.rh, summer, x, y) * 100, 0.5 * (rainS + rainW),
               at(c.cloud, summer, x, y) * 100,
               c.elev[((x % W) + W) % W + y * W] > 0 ? "" : "  <- sea");
    }

    // And the winter pole, where the model is 25 degrees too warm: the whole
    // surface budget, term by term.
    int py = H - 3;
    int px = 0;
    double bestT = 1e9;
    for (int x = 0; x < W; x++)
        if (at(c.meanT, 0, x, py) < bestT) { bestT = at(c.meanT, 0, x, py); px = x; }
    double Ts = at(c.meanT, 0, px, py) + 273.15, Ta = at(c.airT, 0, px, py) + 273.15;
    double sig = 5.670374e-8;
    fprintf(stderr,
            "\nWinter pole (%.0f deg), coldest cell: surface %.1f C, air %.1f C\n"
            "  surface emits  %6.1f W/m2\n"
            "  air returns    %6.1f W/m2  (emissivity %.3f)\n"
            "  net loss       %6.1f W/m2  <- life is about 40-60 over polar ice\n"
            "  water in air   %6.2f mm    <- life is under 2 in a polar winter\n",
            ((py + 0.5) / H - 0.5) * 180.0, Ts - 273.15, Ta - 273.15, sig * Ts * Ts * Ts * Ts,
            atmosphere::emissOf(at(c.wv, 0, px, py)) * sig * Ta * Ta * Ta * Ta,
            atmosphere::emissOf(at(c.wv, 0, px, py)),
            sig * Ts * Ts * Ts * Ts -
                atmosphere::emissOf(at(c.wv, 0, px, py)) * sig * Ta * Ta * Ta * Ta,
            at(c.wv, 0, px, py));
    return 0;
}
