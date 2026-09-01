// Climate parameter sweep. Builds one world's terrain and hydrology, then
// runs the atmosphere over a grid of settings, scoring each against the
// targets in Design/Weather. Terrain is the slow part and is built once, so
// each candidate costs only its own atmosphere run.
//
//   cl /O2 /openmp /EHsc /std:c++17 src\sweep.cpp /Fe:build\sweep.exe
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>
#include "terrain.h"
#include "hydrology.h"
#include "atmosphere.h"

// What a climate should look like. Land-and-sea zonal means, by season.
struct Target {
    const char* name;
    float lat;
    int season; // 0 DJF, 2 JJA
    float want;
    float weight;
};
static const Target TARGETS[] = {
    {"equator", 0, 2, 27, 1.0f},      {"subtropics", 25, 2, 30, 1.0f},
    {"mid-lat summer", 50, 2, 20, 1.5f}, {"mid-lat winter", 50, 0, -5, 1.5f},
    {"60N summer", 62, 2, 15, 1.5f},  {"60N winter", 62, 0, -25, 1.5f},
    {"polar summer", 82, 2, 0, 1.0f}, {"polar winter", 82, 0, -45, 1.0f},
};

struct Score {
    double err = 0, mean = 0, rain = 0, polarRain = 0;
    double desert = 0, landRain = 0; // share of land under 0.5 mm/day, and its mean
    double coastRain = 0, innerRain = 0; // and where on the land it falls
    double spot[8] = {};
};

static Score judge(const atmosphere::Climatology& c) {
    const int AW = atmosphere::W, AH = atmosphere::H;
    Score s;
    double gT = 0, gR = 0, gw = 0;
    for (int y = 0; y < AH; y++) {
        double w = std::cos(((y + 0.5) / (double)AH - 0.5) * 3.14159265);
        for (int x = 0; x < AW; x++)
            for (int se = 0; se < atmosphere::SEASONS; se++) {
                int i = se * AW * AH + y * AW + x;
                gT += c.meanT[i] * w;
                gR += c.rainMmDay[i] * w;
                gw += w;
            }
    }
    s.mean = gT / gw;
    s.rain = gR / gw;
    // How much of the land is desert. About a third of Earth's is arid or
    // semi-arid; a world where nearly all of it is has a rainfall problem
    // that a global mean can hide.
    {
        double dry = 0, land = 0, lr = 0, cr = 0, cn = 0, ir = 0, in_ = 0;
        for (int i = 0; i < AW * AH; i++) {
            if (c.elev[i] <= 0.0f) continue;
            double r = 0;
            for (int se = 0; se < atmosphere::SEASONS; se++) r += c.rainMmDay[se * AW * AH + i];
            r /= atmosphere::SEASONS;
            land += 1;
            lr += r;
            if (r < 0.5) dry += 1;
            // Coast or interior: is there sea within two cells?
            int x = i % AW, y = i / AW;
            bool coastal = false;
            for (int dy = -2; dy <= 2 && !coastal; dy++)
                for (int dx = -2; dx <= 2 && !coastal; dx++) {
                    int yy = y + dy;
                    if (yy < 0 || yy >= AH) continue;
                    int xx = ((x + dx) % AW + AW) % AW;
                    if (c.elev[yy * AW + xx] <= 0.0f) coastal = true;
                }
            if (coastal) { cr += r; cn += 1; } else { ir += r; in_ += 1; }
        }
        s.coastRain = cn > 0 ? cr / cn : 0;
        s.innerRain = in_ > 0 ? ir / in_ : 0;
        s.desert = land > 0 ? dry / land : 0;
        s.landRain = land > 0 ? lr / land : 0;
    }
    int k = 0;
    for (const Target& t : TARGETS) {
        int y = (int)((t.lat / 180.0f + 0.5f) * AH);
        y = y < 0 ? 0 : (y >= AH ? AH - 1 : y);
        double sum = 0, rn = 0;
        for (int x = 0; x < AW; x++) {
            sum += c.meanT[t.season * AW * AH + y * AW + x];
            rn += c.rainMmDay[t.season * AW * AH + y * AW + x];
        }
        double got = sum / AW;
        s.spot[k++] = got;
        double d = (got - t.want) / 10.0; // a decade of error is one unit
        s.err += t.weight * d * d;
        if (t.lat > 80) s.polarRain = std::max(s.polarRain, rn / AW);
    }
    // The world as a whole matters as much as any one latitude.
    double dm = (s.mean - 15.0) / 5.0, dr = (s.rain - 2.7) / 1.0;
    s.err += 3.0 * dm * dm + 2.0 * dr * dr;
    // Poles are deserts: rain there above half a mm a day is the latent pump.
    double dp = std::max(s.polarRain - 0.5, 0.0);
    s.err += 2.0 * dp * dp;
    // Desert share, and the cold end of the world, both weighted like a spot.
    double dd = (s.desert - 0.30) / 0.15;
    s.err += 3.0 * dd * dd;
    return s;
}

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

    fprintf(stderr, "terrain once...\n");
    plates::Field pf = plates::build(seed);
    float seaLevel = terrain::seaLevelFor(landPct / 100.0f, cp, rot, offset, pf);
    hydrology::Result hy = hydrology::build(cp, seaLevel, rot, offset, 12000.0f, pf);
    fprintf(stderr, "sweeping...\n");

    // Round two. The first grid showed the trade clearly: a thin air layer
    // gives seasons and hands the poles to the latent pump, a thick one is
    // stable and freezes the summers. The suspect is the coupling itself --
    // land is welded to an air layer that diffusion has smeared into a
    // hemispheric mean, so a continent cannot have its own summer.
    // Round three, at the corner neither of the first two visited: weak
    // surface-air coupling (which gives continents their summer) together
    // with weak frontal lifting (which is what feeds the polar pump).
    // Round four. Everything but the poles is on target; what is left is that
    // moisture DIFFUSES to the pole rather than travelling by wind and raining
    // its way there, so it arrives with its whole load. K_DIFF is that pipe.
    // With transport conservative at last, the pump is bounded and the world
    // can be warmed and watered without it running away.
    const double cAir[] = {0.18};                 // EVAP_WATER
    const double kdiff2[] = {1.5e6};               // KT_DIFF
    const double ktDiff[] = {0.17};               // CLOUD_ALBEDO
    const double kSurf[] = {5.0};
    const double emiss[] = {0.975, 0.99};

    double best = 1e30;
    std::string bestName;
    for (double ca2 : cAir)
      for (double kd : kdiff2)
        for (double kt : ktDiff)
            for (double wf : kSurf)
                for (double em : emiss) {
                    atmosphere::KT_DIFF = kd;
                    atmosphere::C_AIR = 1.0e7;
                    atmosphere::K_DIFF = 2.0e5;
                    atmosphere::EVAP_WATER = ca2;
                    atmosphere::EVAP_LAND = ca2 * 0.28;
                    atmosphere::CLOUD_ALBEDO = kt;
                    atmosphere::K_STABLE = 0.8;

                    atmosphere::K_SURF_AIR = wf;
                    atmosphere::W_FRONT = 200.0;
                    atmosphere::EMISS = em;
                    atmosphere::Climatology c =
                        atmosphere::build(cp, seaLevel, rot, offset, pf, hy, false);
                    Score s = judge(c);
                    char line[512];
                    snprintf(line, sizeof line,
                             "EV %.2f KT %.1e CA %.2f KS %4.1f EM %.3f | err %6.1f | mean %5.1f rain "
                             "%4.2f pRain %4.1f dry %3.0f%% | eq %5.1f sub %5.1f mls %5.1f mlw %5.1f 60s "
                             "%5.1f 60w %5.1f ps %5.1f pw %5.1f",
                             ca2, kd, kt, wf, em, s.err, s.mean, s.rain, s.polarRain, s.desert * 100, s.spot[0],
                             s.spot[1], s.spot[2], s.spot[3], s.spot[4], s.spot[5], s.spot[6],
                             s.spot[7]);
                    fprintf(stderr, "%s | Wv %5.2f mm, %4.1f d, wind %4.1f m/s, RH %3.0f%%, coast %4.2f inland %4.2f\n", line, c.dbgWv,
                            c.dbgWv / std::max(c.dbgRain, 1e-6), c.dbgWind, c.dbgRH * 100, s.coastRain,
                            s.innerRain);
                    fflush(stderr);
                    if (s.err < best) {
                        best = s.err;
                        bestName = line;
                    }
                }
    fprintf(stderr, "\nBEST\n%s\n", bestName.c_str());
    return 0;
}
