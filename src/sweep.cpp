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
    double desert = 0, landRain = 0, cloud = 0; // share of land under 0.5 mm/day, and its mean
    double coastRain = 0, innerRain = 0; // and where on the land it falls
    double spot[8] = {};
};

// what still air at this temperature could hold, for the ratio below
static double capAirRef(double Ta) {
    return atmosphere::capAirOf(Ta);
}

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
                s.cloud += c.cloud[i] * w;
                gR += c.rainMmDay[i] * w;
                gw += w;
            }
    }
    s.mean = gT / gw;
    s.cloud /= gw;
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

    // The sweep is no longer a sweep. Everything it used to vary --
    // surface-air exchange, evaporation, cloud albedo, emissivity -- has a
    // measured value or a measured formula, and is now set from that rather
    // than fitted to the score. What is left is a single evaluation, and its
    // job is not to find a good setting but to report the RESIDUAL: the part
    // of the error that no correct value explains, which is the part that
    // names the mechanism still missing.
    {
        atmosphere::Climatology c = atmosphere::build(cp, seaLevel, rot, offset, pf, hy, false);
        Score s = judge(c);
        const int AW = atmosphere::W, AH = atmosphere::H;
        fprintf(stderr,
                "PHYSICAL | err %6.1f | mean %5.1f rain %4.2f pRain %4.1f dry %3.0f%% cloud %3.0f%%\n"
                "  eq %5.1f sub %5.1f mls %5.1f mlw %5.1f 60s %5.1f 60w %5.1f ps %5.1f pw %5.1f\n"
                "  want   27      30       20      -5      15     -25       0     -45\n"
                "  Wv %5.2f mm, residence %4.1f d, wind %4.1f m/s, RH %3.0f%%, coast %4.2f inland %4.2f\n",
                s.err, s.mean, s.rain, s.polarRain, s.desert * 100, s.cloud * 100, s.spot[0], s.spot[1],
                s.spot[2], s.spot[3], s.spot[4], s.spot[5], s.spot[6], s.spot[7], c.dbgWv,
                c.dbgWv / std::max(c.dbgRain, 1e-6), c.dbgWind, c.dbgRH * 100, s.coastRain,
                s.innerRain);
        // The zonal profile, every four degrees. Spot latitudes hide
        // inversions: a reading of -32 at 62 degrees next to -4 at 82 is not
        // a calibration error, it is something structurally wrong in between,
        // and only the whole curve says where.
        fprintf(stderr, "\n%6s %7s %7s %7s %7s %7s %7s %7s %7s %7s %7s %7s\n", "lat", "DJF", "JJA", "rain", "Wv",
                "cap", "wConv", "wDiv", "wFrnt", "wOro", "wSum", "capR");
        for (int y = AH - 2; y >= 1; y -= 2) {
            double la = ((y + 0.5) / (double)AH - 0.5) * 180.0;
            double w = 0, sm = 0, ai = 0, rn = 0, cl = 0, ld = 0, ev = 0, wv = 0;
            double uc = 0, ud = 0, uf = 0, uo = 0, uu = 0, vv = 0, umx = 0, dmx = 0;
            for (int x = 0; x < AW; x++) {
                w += c.meanT[0 * AW * AH + y * AW + x];
                sm += c.meanT[2 * AW * AH + y * AW + x];
                ai += c.airT[2 * AW * AH + y * AW + x];
                for (int se = 0; se < atmosphere::SEASONS; se++)
                    rn += c.rainMmDay[se * AW * AH + y * AW + x] / atmosphere::SEASONS;
                cl += c.cloud[2 * AW * AH + y * AW + x];
                ld += c.elev[y * AW + x] > 0 ? 1 : 0;
                ev += std::max(c.elev[y * AW + x], 0.0f);
                wv += c.wv[2 * AW * AH + y * AW + x];
                uc += c.upConv[2 * AW * AH + y * AW + x];
                ud += c.upDiv[2 * AW * AH + y * AW + x];
                uf += c.upFront[2 * AW * AH + y * AW + x];
                uo += c.upOrog[2 * AW * AH + y * AW + x];
                double uh = c.windU[2 * AW * AH + y * AW + x];
                double vh = c.windV[2 * AW * AH + y * AW + x];
                uu += uh;
                vv += vh;
                umx = std::max(umx, std::sqrt(uh * uh + vh * vh));
                dmx += c.capX[2 * AW * AH + y * AW + x];
            }
            double wsum = (uc + ud + uf + uo) / AW;
            fprintf(stderr,
                    "%6.0f %7.1f %7.1f %7.2f %7.2f %7.2f %7.4f %7.4f %7.4f %7.4f %7.4f %7.2f\n",
                    la, w / AW, sm / AW, rn / AW, wv / AW, dmx / AW, uc / AW, ud / AW, uf / AW,
                    uo / AW, wsum, (dmx / AW) / std::max(0.05, (double)0.0 + capAirRef(ai / AW)));
            (void)ev; (void)uc; (void)uf; (void)uo; (void)ld;
            (void)ev;
            (void)ev;
        }
        return 0;
    }
    return 0;
}
