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
        if (argc >= 3) atmosphere::SPINUP_DAYS = atoi(argv[2]) * 365;
        fprintf(stderr, "spin-up %d days\n", atmosphere::SPINUP_DAYS);
        atmosphere::Climatology c = atmosphere::build(cp, seaLevel, rot, offset, pf, hy, false);
        Score s = judge(c);
        const int AW = atmosphere::W, AH = atmosphere::H;
        fprintf(stderr,
                "PHYSICAL | err %6.1f | mean %5.1f rain %4.2f pRain %4.1f dry %3.0f%% cloud %3.0f%%\n"
                "  eq %5.1f sub %5.1f mls %5.1f mlw %5.1f 60s %5.1f 60w %5.1f ps %5.1f pw %5.1f\n"
                "  want   27      30       20      -5      15     -25       0     -45\n"
                "  Wv %5.2f mm, residence %4.1f d, wind %4.1f m/s, RH %3.0f%%, coast %4.2f inland %4.2f\n"
                "  water: evap %5.2f rain %5.2f clamped %+6.3f mm/day\n",
                s.err, s.mean, s.rain, s.polarRain, s.desert * 100, s.cloud * 100, s.spot[0], s.spot[1],
                s.spot[2], s.spot[3], s.spot[4], s.spot[5], s.spot[6], s.spot[7], c.dbgWv,
                c.dbgWv / std::max(c.dbgRain, 1e-6), c.dbgWind, c.dbgRH * 100, s.coastRain,
                s.innerRain, c.dbgEvap, c.dbgRain, c.dbgClamp);
        // What the MAP actually shows. Green is not where it rains, it is where
        // rain beats evaporative demand -- m = 0.5*rain/pet with pet rising
        // steeply with temperature -- so the same rainfall is desert when warm
        // and grassland when cool. If the green is sitting on the high ground
        // rather than near the sea, this is why.
        {
            const int NB = 5;
            double edge[NB + 1] = {0, 250, 600, 1200, 2000, 9999};
            double mSum[NB] = {0}, rSum[NB] = {0}, tSum[NB] = {0}, n[NB] = {0};
            double cM = 0, cN = 0, iM = 0, iN = 0;
            for (int i = 0; i < AW * AH; i++) {
                if (c.elev[i] <= 0.0f) continue;
                double rain = 0, t = 0;
                for (int se = 0; se < atmosphere::SEASONS; se++) {
                    rain += c.rainMmDay[se * AW * AH + i] / atmosphere::SEASONS;
                    t += c.meanT[se * AW * AH + i] / atmosphere::SEASONS;
                }
                double pet = std::max(0.4, 0.11 * (t + 8.0));
                double m = std::clamp(0.5 * rain / pet, 0.0, 1.0);
                double h = c.elev[i];
                for (int b = 0; b < NB; b++)
                    if (h >= edge[b] && h < edge[b + 1]) {
                        mSum[b] += m; rSum[b] += rain; tSum[b] += t; n[b] += 1;
                    }
                int x = i % AW, y = i / AW;
                bool coastal = false;
                for (int dy = -2; dy <= 2 && !coastal; dy++)
                    for (int dx = -2; dx <= 2 && !coastal; dx++) {
                        int yy = y + dy;
                        if (yy < 0 || yy >= AH) continue;
                        int xx = ((x + dx) % AW + AW) % AW;
                        if (c.elev[yy * AW + xx] <= 0.0f) coastal = true;
                    }
                if (coastal) { cM += m; cN += 1; } else { iM += m; iN += 1; }
            }
            fprintf(stderr, "\nWHAT THE MAP SHOWS: moisture = 0.5*rain/pet\n");
            fprintf(stderr, "  %-16s %8s %8s %8s %8s\n", "elevation", "moisture", "rain",
                    "degC", "cells");
            for (int b = 0; b < NB; b++) {
                if (n[b] < 1) continue;
                fprintf(stderr, "  %5.0f - %-8.0f %8.2f %8.2f %8.1f %8.0f\n", edge[b],
                        edge[b + 1], mSum[b] / n[b], rSum[b] / n[b], tSum[b] / n[b], n[b]);
            }
            fprintf(stderr, "  %-16s %8.2f\n  %-16s %8.2f\n", "coastal land",
                    cN > 0 ? cM / cN : 0.0, "interior land", iN > 0 ? iM / iN : 0.0);
            fprintf(stderr, "  (0.3 is about where desert gives way to grass)\n");
        }

        // Where the height comes from. The land is 2188 m in the mean against
        // life's 840, and the total is
        //
        //   h = continent + detail + ranges*uplift + hills,   all times 8000 m
        //
        // so the question is which term carries it: the base continental field
        // that sea level is cut from, or the mountains piled on top.
        {
            double nL = 0, base = 0, tot = 0, upl = 0;
            double q10 = 0, q50 = 0, q90 = 0;
            std::vector<double> baseV;
            for (int y = 0; y < hydrology::H; y += 3)
                for (int x = 0; x < hydrology::W; x += 3) {
                    double lat = ((y + 0.5) / hydrology::H - 0.5) * 3.14159265;
                    double lon = ((x + 0.5) / hydrology::W * 2.0 - 1.0) * 3.14159265;
                    terrain::V3 n{(float)(std::cos(lat) * std::cos(lon)),
                                  (float)(std::cos(lat) * std::sin(lon)), (float)std::sin(lat)};
                    terrain::V3 w = terrain::rotate(rot, n) + offset;
                    plates::Cell pl = pf.sample({n.x, n.y, n.z});
                    double cont = terrain::continentField(w, cp) +
                                  pl.crust * terrain::CRUST_WEIGHT - seaLevel;
                    float h = hy.heightM[y * hydrology::W + x];
                    if (h <= 0) continue;
                    nL += 1;
                    base += cont * terrain::HEIGHT_SCALE_M * terrain::LAND_RELIEF;
                    tot += h;
                    upl += std::max(pl.uplift, 0.0f);
                    baseV.push_back(cont * terrain::HEIGHT_SCALE_M * terrain::LAND_RELIEF);
                }
            std::sort(baseV.begin(), baseV.end());
            if (!baseV.empty()) {
                q10 = baseV[baseV.size() / 10];
                q50 = baseV[baseV.size() / 2];
                q90 = baseV[baseV.size() * 9 / 10];
            }
            fprintf(stderr,
                    "\nWHERE THE HEIGHT COMES FROM (land cells)\n"
                    "  total height            %7.0f m\n"
                    "  of which base field     %7.0f m   <- what sea level cuts\n"
                    "  of which mountains etc  %7.0f m\n"
                    "  base field p10/50/90    %7.0f %7.0f %7.0f m\n"
                    "  mean uplift             %7.2f\n",
                    tot / std::max(nL, 1.0), base / std::max(nL, 1.0),
                    (tot - base) / std::max(nL, 1.0), q10, q50, q90, upl / std::max(nL, 1.0));
        }

        // And straight from the hydrology raster, before the atmosphere coarsens
        // anything, so a smoothing artefact cannot be blamed.
        {
            double n = 0, sum = 0, hi = 0, hi4 = 0, mx = 0;
            for (int i = 0; i < hydrology::W * hydrology::H; i++) {
                float h = hy.heightM[i];
                if (h <= 0) continue;
                n += 1; sum += h;
                if (h > 2000) hi += 1;
                if (h > 4000) hi4 += 1;
                mx = std::max(mx, (double)h);
            }
            fprintf(stderr,
                    "\nTERRAIN, straight from the hydrology raster\n"
                    "  mean land elevation   %7.0f m   life 840\n"
                    "  land above 2 km       %7.1f%%   life about 5\n"
                    "  land above 4 km       %7.1f%%   life about 1\n"
                    "  highest point         %7.0f m   life 8848\n",
                    sum / std::max(n, 1.0), 100 * hi / std::max(n, 1.0),
                    100 * hi4 / std::max(n, 1.0), mx);
        }

        // Stop inferring what the map shows and ask it. The same mixtureAt the
        // renderer calls, over every land cell, with the climate this run
        // produced.
        {
            const char* NAME[11] = {"bare",   "tundra", "taiga",    "forest",  "rainforest",
                                    "grass",  "steppe", "savanna",  "shrub",   "marsh",
                                    "desert"};
            const double LIFE[11] = {8, 10, 10, 21, 6, 9, 8, 8, 6, 2, 12}; // rough land shares
            double cov[11] = {0}, tot = 0, hiCold = 0;
            double bHigh = 0, bIce = 0, bCold = 0, bDry = 0, bT = 0, dom[11] = {0};
            for (int i = 0; i < AW * AH; i++) {
                if (c.elev[i] <= 0.0f) continue;
                int x = i % AW, y = i / AW;
                double lat = ((y + 0.5) / (double)AH - 0.5) * 3.14159265;
                double lon = ((x + 0.5) / (double)AW * 2.0 - 1.0) * 3.14159265;
                terrain::V3 n{(float)(std::cos(lat) * std::cos(lon)),
                              (float)(std::cos(lat) * std::sin(lon)), (float)std::sin(lat)};
                terrain::V3 w = terrain::rotate(rot, n) + offset;
                double rain = 0, t = 0, tc = 1e9, tw = -1e9;
                for (int se = 0; se < atmosphere::SEASONS; se++) {
                    rain += c.rainMmDay[se * AW * AH + i] / atmosphere::SEASONS;
                    t += c.meanT[se * AW * AH + i] / atmosphere::SEASONS;
                    tc = std::min(tc, (double)c.meanT[se * AW * AH + i]);
                    tw = std::max(tw, (double)c.meanT[se * AW * AH + i]);
                }
                double pet = std::max(0.4, 0.11 * (t + 8.0));
                float m = (float)std::clamp(0.5 * rain / pet, 0.0, 1.0) +
                          terrain::moistureDetail(w);
                terrain::Mixture mx = terrain::mixtureAt(c.elev[i], 0.0f, (float)t,
                                                         std::clamp(m, 0.0f, 1.0f), 0.0f, false,
                                                         terrain::patchNoise(w), 0.0f, (float)tc,
                                                         (float)tw);
                for (int k = 0; k < 11; k++) cov[k] += mx.cov[k];
                // The mean fraction is ambiguous -- every cell a third bare
                // and a third of cells wholly bare give the same number, and
                // they look nothing alike. The eye reads the dominant class,
                // so count that too. Bare has no colour of its own: it shows
                // the substrate under it, which is why bare ground reads as
                // desert on the map whatever the desert class says.
                int best = 0;
                for (int k = 1; k < 11; k++)
                    if (mx.cov[k] > mx.cov[best]) best = k;
                dom[best] += 1;
                tot += 1;
                if (c.elev[i] > 2000.0f) hiCold += 1;
                // Bare overrides whatever vegetation was computed, and four
                // things can raise it. Which one is doing it here? Slope and
                // uplift are zero on this grid -- its elevation is a block
                // mean over 208 km, so a slope read off it would be fiction --
                // so this is the floor, and the rendered world adds rock and
                // scree on the steep ground on top of what this shows.
                double mm = std::clamp(m, 0.0f, 1.0f);
                bHigh += terrain::smoothstep(3200.0f, 3900.0f, c.elev[i]);
                bIce += terrain::smoothstep(0.0f, -4.0f, (float)tw);
                bCold += terrain::smoothstep(2.0f, -2.0f, (float)tw);
                bDry += terrain::smoothstep(0.1f, 0.03f, (float)mm) * 0.5f;
                bT += t;
            }
            fprintf(stderr, "\nLAND COVER, from the renderer's own mixtureAt\n");
            fprintf(stderr, "  %-12s %8s %8s %8s\n", "", "mean", "dominant", "life");
            for (int k = 0; k < 11; k++)
                fprintf(stderr, "  %-12s %7.1f%% %7.1f%% %7.0f%%\n", NAME[k],
                        100 * cov[k] / std::max(tot, 1.0), 100 * dom[k] / std::max(tot, 1.0),
                        LIFE[k]);
            fprintf(stderr, "  %-12s %6.1f%%   life about   5%%\n", "land >2km",
                    100 * hiCold / std::max(tot, 1.0));
            // Land at 0.1 C against life's 8.5 while the globe is only 2 K low.
            // Is the land simply lying in cold latitudes in this world, or is
            // it too cold for the latitude it lies in? Only a comparison
            // against the sea at the SAME latitude can tell the two apart.
            fprintf(stderr, "\nLAND AGAINST THE SEA AT THE SAME LATITUDE\n");
            fprintf(stderr, "  %5s %8s %8s %8s %8s %8s\n", "lat", "land C", "sea C", "diff",
                    "elev m", "land%");
            for (int y0 = 0; y0 < AH; y0 += 8) {
                double lT = 0, sT = 0, lN = 0, sN = 0, hSum = 0;
                for (int y = y0; y < std::min(y0 + 8, AH); y++)
                    for (int x = 0; x < AW; x++) {
                        int i = y * AW + x;
                        double t = 0;
                        for (int se = 0; se < atmosphere::SEASONS; se++)
                            t += c.meanT[se * AW * AH + i] / atmosphere::SEASONS;
                        if (c.elev[i] > 0.0f) { lT += t; lN += 1; hSum += c.elev[i]; }
                        else { sT += t; sN += 1; }
                    }
                if (lN < 1 && sN < 1) continue;
                double lat = ((y0 + 4.0) / AH - 0.5) * 180.0;
                fprintf(stderr, "  %5.0f %8.1f %8.1f %8.1f %8.0f %8.0f\n", lat,
                        lN > 0 ? lT / lN : 0.0, sN > 0 ? sT / sN : 0.0,
                        (lN > 0 && sN > 0) ? lT / lN - sT / sN : 0.0,
                        lN > 0 ? hSum / lN : 0.0, 100 * lN / std::max(lN + sN, 1.0));
            }
            fprintf(stderr,
                    "  what makes it bare: high %4.1f%%  ice %4.1f%%  cold %4.1f%%  dry %4.1f%%"
                    "   (mean land %4.1f C)\n",
                    100 * bHigh / std::max(tot, 1.0), 100 * bIce / std::max(tot, 1.0),
                    100 * bCold / std::max(tot, 1.0), 100 * bDry / std::max(tot, 1.0),
                    bT / std::max(tot, 1.0));
        }

        // The zonal profile, every four degrees. Spot latitudes hide
        // inversions: a reading of -32 at 62 degrees next to -4 at 82 is not
        // a calibration error, it is something structurally wrong in between,
        // and only the whole curve says where.
        // The height field itself, against what the thermal target is asking
        // of it. If hP is not tracking hWant the pressure gradient is not the
        // hypsometric one whatever the coefficient says, and the wind cannot
        // be either.
        fprintf(stderr, "\n%6s %7s %8s %8s %7s %7s\n", "lat", "Tair", "hP", "hWant",
                "u", "v");
        for (int y = AH - 2; y >= 1; y -= 4) {
            double la = ((y + 0.5) / (double)AH - 0.5) * 180.0;
            double ta = 0, hp = 0, uu = 0, vv = 0;
            for (int x = 0; x < AW; x++) {
                int j = 2 * AW * AH + y * AW + x;
                ta += c.airT[j];
                hp += c.press[j];
                uu += c.windU[j];
                vv += c.windV[j];
            }
            ta /= AW; hp /= AW; uu /= AW; vv /= AW;
            fprintf(stderr, "%6.0f %7.1f %8.1f %8.1f %7.1f %7.1f\n", la, ta, hp,
                    atmosphere::THERM_H_PER_K * (ta + 25.0), uu, vv);
        }
        // Cloud, against what is actually up there. The global mean is the
        // easy half and it already matches; the pattern is the half that
        // decides whether a planet LOOKS right, because Earth's cloud is
        // clumped -- an overcast storm track, a clear subtropical high -- and
        // a uniform sixty percent everywhere would hit the same mean while
        // looking nothing like it.
        //
        // Observed zonal-mean total cloud amount, for the column on the right.
        static const struct { int lat; int obs; } OBS[] = {
            {85, 70}, {75, 73}, {65, 75}, {55, 74}, {45, 68}, {35, 57},
            {25, 50}, {15, 53}, {5, 66},  {-5, 66}, {-15, 53}, {-25, 52},
            {-35, 62}, {-45, 76}, {-55, 82}, {-65, 78}, {-75, 72}, {-85, 65},
        };
        fprintf(stderr, "\n%6s %7s %7s %7s %7s %7s\n", "lat", "cloud", "observed",
                "RH", "land", "sea");
        for (size_t k = 0; k < sizeof OBS / sizeof OBS[0]; k++) {
            int y = (int)((OBS[k].lat / 180.0 + 0.5) * AH);
            y = y < 0 ? 0 : (y >= AH ? AH - 1 : y);
            double cl = 0, rh = 0, cLand = 0, nLand = 0, cSea = 0, nSea = 0;
            for (int x = 0; x < AW; x++)
                for (int se = 0; se < atmosphere::SEASONS; se++) {
                    int j = se * AW * AH + y * AW + x;
                    cl += c.cloud[j];
                    rh += c.rh[j];
                    if (c.elev[y * AW + x] > 0.0f) { cLand += c.cloud[j]; nLand += 1; }
                    else { cSea += c.cloud[j]; nSea += 1; }
                }
            double n = AW * (double)atmosphere::SEASONS;
            fprintf(stderr, "%6d %6.0f%% %6d%% %6.0f%% %6.0f%% %6.0f%%\n", OBS[k].lat,
                    100 * cl / n, OBS[k].obs, 100 * rh / n, nLand > 0 ? 100 * cLand / nLand : 0.0,
                    nSea > 0 ? 100 * cSea / nSea : 0.0);
        }
        // And the distribution. Earth has genuinely clear skies and genuinely
        // overcast ones; a model whose every cell sits near the mean has the
        // right average and the wrong sky.
        {
            int bins[5] = {0, 0, 0, 0, 0};
            double tot = 0;
            for (int i = 0; i < AW * AH * atmosphere::SEASONS; i++) {
                double v = c.cloud[i];
                int k = (int)std::min(4.0, std::floor(v * 5.0));
                bins[k < 0 ? 0 : k]++;
                tot += 1;
            }
            fprintf(stderr,
                    "\ncloud spread   <20%%:%4.0f%%  20-40:%4.0f%%  40-60:%4.0f%%  "
                    "60-80:%4.0f%%  >80%%:%4.0f%%\n",
                    100 * bins[0] / tot, 100 * bins[1] / tot, 100 * bins[2] / tot,
                    100 * bins[3] / tot, 100 * bins[4] / tot);
            // No observed row here, deliberately. The SHAPE is established --
            // cloud fraction over a grid box is bimodal, clear and overcast, which
            // is why current schemes diagnose it from two Gaussian modes rather
            // than one (Van Weverberg et al. 2021) -- but exact bin fractions
            // depend on the sensor, the cloud threshold and the box size. An
            // earlier version of this line carried numbers that were an estimate
            // dressed as data, which is worse than no row at all.
            //
            // The diagnosis does not need them: 84% of this model in ONE bin,
            // with nothing clear and nothing overcast, is unimodal where life is
            // not, and that is the whole finding.
        }
        return 0;
    }
    return 0;
}
