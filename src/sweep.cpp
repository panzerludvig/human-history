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
    // "earth" as the seed is the template globe (see terrain::TEMPLATE).
    bool earth = argc >= 2 && _stricmp(argv[1], "earth") == 0;
    uint32_t seed = earth ? 1u : (argc >= 2 ? (uint32_t)strtoul(argv[1], nullptr, 10) : 7);
    if (earth) {
        if (!terrain::loadTemplate("../data/earth.bin") && !terrain::loadTemplate("data/earth.bin")) {
            fprintf(stderr, "data/earth.bin not found (run tools/make_earth.py)\n");
            return 1;
        }
        terrain::TEMPLATE.active = true;
    }
    std::string tag = earth ? "earth" : std::to_string(seed);
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
    // The same bilinear the climate samplers use, but weighted by whether the
    // cell is land. Interpolating a land temperature out of ocean cells is the
    // wider version of the majority-vote problem: a land point need only be
    // within half a cell of the sea -- plus the 64 km climFuzz displacement --
    // to be handed a sea-weighted average, and near the poles the sea runs
    // 7 to 15 K warmer than the land beside it.
    auto landMaskedT = [](const atmosphere::Climatology& c, int season, terrain::V3 n,
                          bool& anyLand) {
        const int W = atmosphere::W, H = atmosphere::H;
        float lat = std::asin(std::clamp(n.z, -1.0f, 1.0f));
        float lon = std::atan2(n.y, n.x);
        float u = ((lon + 3.14159265f) / (2 * 3.14159265f)) * W - 0.5f;
        float vv = ((lat + 3.14159265f / 2) / 3.14159265f) * H - 0.5f;
        int x0 = (int)std::floor(u), y0 = (int)std::floor(vv);
        float fx = u - x0, fy = vv - y0;
        double num = 0, den = 0;
        anyLand = false;
        for (int j = 0; j <= 1; j++)
            for (int i = 0; i <= 1; i++) {
                int xx = ((x0 + i) % W + W) % W, yy = std::clamp(y0 + j, 0, H - 1);
                double wgt = (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
                if (c.elev[yy * W + xx] <= 0.0f) continue;
                anyLand = true;
                num += c.meanT[season * W * H + yy * W + xx] * wgt;
                den += wgt;
            }
        return den > 1e-6 ? (float)(num / den) : 0.0f;
    };

    // Before anything else: how much of the world does the land/sea majority
    // vote actually misrepresent? A cell 40% land is modelled as pure ocean and
    // a cell 60% land as pure continent, so every mixed cell is wrong about one
    // part of itself. If mixed cells are rare the vote is a fair approximation
    // and fractional land is not worth the work; if they are common it is not
    // an approximation at all.
    {
        const int AW = atmosphere::W, AH = atmosphere::H;
        double hist[5] = {0}, wgt = 0, mixed = 0, landLost = 0, seaLost = 0, landAll = 0;
        for (int y = 0; y < AH; y++) {
            double cw = std::cos(((y + 0.5) / AH - 0.5) * 3.14159265);
            for (int x = 0; x < AW; x++) {
                // Proportional slices, as the atmosphere now takes them (the
                // integer division that stood here shrank the map by 6%).
                int x0 = (int)((long long)x * hydrology::W / AW), x1 = (int)((long long)(x + 1) * hydrology::W / AW);
                int y0 = (int)((long long)y * hydrology::H / AH), y1 = (int)((long long)(y + 1) * hydrology::H / AH);
                double land = 0, cells = 0;
                for (int yy = y0; yy < y1; yy++)
                    for (int xx = x0; xx < x1; xx++) {
                        if (hy.heightM[yy * hydrology::W + xx] > 0) land++;
                        cells++;
                    }
                double lf = land / std::max(cells, 1.0);
                int b = lf < 0.02 ? 0 : (lf < 0.25 ? 1 : (lf < 0.75 ? 2 : (lf < 0.98 ? 3 : 4)));
                hist[b] += cw;
                wgt += cw;
                if (lf > 0.02 && lf < 0.98) mixed += cw;
                landAll += lf * cw;
                if (lf < 0.5) landLost += lf * cw;              // land called ocean
                else seaLost += (1.0 - lf) * cw;                // ocean called land
            }
        }
        const char* L[5] = {"all sea      ", "under 1/4 land", "mixed        ",
                            "over 3/4 land", "all land     "};
        fprintf(stderr, "\nWHAT THE LAND/SEA VOTE COSTS (area-weighted cells)\n");
        for (int b = 0; b < 5; b++)
            fprintf(stderr, "  %s %6.1f%%\n", L[b], 100 * hist[b] / std::max(wgt, 1.0));
        fprintf(stderr, "  cells that are neither purely one nor the other: %.1f%%\n",
                100 * mixed / std::max(wgt, 1.0));
        fprintf(stderr, "  land modelled as ocean: %.1f%% of all land\n",
                100 * landLost / std::max(landAll, 1e-9));
        fprintf(stderr, "  ocean modelled as land: %.1f%% of the planet\n\n",
                100 * seaLost / std::max(wgt, 1.0));
    }

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
        if (argc >= 4 && std::string(argv[3]) == "phys") atmosphere::PRESCRIBED = false;
        if (argc >= 4 && std::string(argv[3]) == "dyn") atmosphere::DYN2 = true;
        if (argc >= 5) atmosphere::STAT_YEARS = std::max(1, atoi(argv[4]));
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
        // The tropical-ocean column, term by term, against measurement. SST
        // there is set by this balance and almost nothing else -- no
        // arrangement of continents moves it more than a few degrees -- so
        // whichever term stands furthest from its measured value names the
        // energy leak, on any world. Earth references: CERES/ERA-era means
        // for open tropical ocean. One honest asymmetry: Earth's tropical
        // surface keeps a ~30 W/m2 surplus that ocean currents export; this
        // model has no currents, so its net must close at zero instead.
        {
            const double* b = c.tropBud;
            double net = b[0] + b[3] - b[2] - b[4] - b[5];
            fprintf(stderr,
                    "\nTROPICAL OCEAN COLUMN (|lat|<15, open sea, W/m2)   model    earth\n"
                    "  SW absorbed at surface                          %7.1f      190\n"
                    "  SW absorbed in the air                          %7.1f       75\n"
                    "  LW up from the surface                          %7.1f      460\n"
                    "  LW down from the sky                            %7.1f      410\n"
                    "  sensible heat off the surface                   %7.1f       10\n"
                    "  latent heat off the surface                     %7.1f      120\n"
                    "  OLR out the top                                 %7.1f      255\n"
                    "  net into the surface                            %7.1f       30 (currents; here: 0)\n"
                    "  Ts %5.1f C (earth 27)   Tb %5.1f C (earth ~23)   Tf %5.1f C (earth ~-18)   Wv %5.1f mm (earth 45)\n"
                    "  emissivity %4.2f (earth ~0.90)   cloud %4.2f (earth ~0.65)   cell-hours %.0f\n",
                    b[0], b[1], b[2], b[3], b[4], b[5], b[6], net,
                    b[7], b[8], b[12], b[9], b[10], b[11], b[13]);
        }

        // Where the pole-to-equator contrast is made and where it is lost.
        // Top of atmosphere first: what each band absorbs of the sun and
        // what it radiates away, against Earth's annual zonal means (CERES
        // era, rounded). The difference is what the atmosphere has to carry
        // poleward, and its running integral from the south pole is the
        // transport across each latitude in petawatts; Earth's peaks near
        // 5.5 PW at 35-40 degrees. Then the terms that carry it: the
        // boundary layer's wind and eddies, the free troposphere's eddies,
        // and the overturning -- deposit into the boundary layer, detrained
        // air into the free troposphere, and the pool's exchange -- with the
        // condensation heating that powers the upper branch.
        {
            static const struct { double lat, sw, olr; } EARTH[] = {
                {0, 318, 250}, {15, 305, 260}, {30, 265, 255}, {45, 205, 235},
                {60, 145, 210}, {75, 100, 190}, {90, 85, 180}};
            auto earthAt = [&](double alat, bool sw) {
                for (int k = 0; k < 6; k++)
                    if (alat <= EARTH[k + 1].lat) {
                        double t = (alat - EARTH[k].lat) / (EARTH[k + 1].lat - EARTH[k].lat);
                        return sw ? EARTH[k].sw + t * (EARTH[k + 1].sw - EARTH[k].sw)
                                  : EARTH[k].olr + t * (EARTH[k + 1].olr - EARTH[k].olr);
                    }
                return sw ? EARTH[6].sw : EARTH[6].olr;
            };
            const int NZ = atmosphere::Climatology::NZB;
            const double R = 6.371e6;
            fprintf(stderr, "\nWHERE THE CONTRAST IS MADE (zonal, W/m2; transport in PW)\n");
            fprintf(stderr, "  %5s | %6s %6s %6s | %6s %6s %6s | %6s %6s | %6s %6s %6s %6s %6s | %6s\n",
                    "lat", "absSW", "OLR", "net", "eSW", "eOLR", "enet", "PW", "ePW",
                    "BLhor", "FThor", "depos", "entr", "pool", "cond");
            double pw = 0, epw = 0;
            std::vector<double> pwRow(AH, 0.0), epwRow(AH, 0.0);
            for (int y = 0; y < AH; y++) {
                double lat = ((y + 0.5) / (double)AH - 0.5) * 180.0;
                double area = 2 * 3.14159265 * R * R * std::cos(lat * 3.14159265 / 180.0) *
                              (3.14159265 / AH);
                const double* z = &c.zonBud[NZ * y];
                pw += (z[0] - z[1]) * area * 1e-15;
                epw += (earthAt(std::fabs(lat), true) - earthAt(std::fabs(lat), false)) * area * 1e-15;
                pwRow[y] = pw; epwRow[y] = epw;
            }
            for (int y0 = 0; y0 < AH; y0 += 8) {
                double s[NZ] = {0}; double n = 0;
                for (int y = y0; y < std::min(y0 + 8, AH); y++) {
                    const double* z = &c.zonBud[NZ * y];
                    for (int k = 0; k < NZ - 1; k++) s[k] += z[k];
                    n += 1;
                }
                for (int k = 0; k < NZ - 1; k++) s[k] /= n;
                double lat = ((y0 + 4.0) / AH - 0.5) * 180.0;
                double alat = std::fabs(lat);
                int ye = std::min(y0 + 7, AH - 1);
                fprintf(stderr, "  %5.0f | %6.0f %6.0f %6.0f | %6.0f %6.0f %6.0f | %6.2f %6.2f | %6.0f %6.0f %6.0f %6.0f %6.0f | %6.0f\n",
                        lat, s[0], s[1], s[0] - s[1], earthAt(alat, true), earthAt(alat, false),
                        earthAt(alat, true) - earthAt(alat, false), pwRow[ye], epwRow[ye],
                        s[7], s[8], s[9], s[10], s[11], s[12]);
            }
            {
                double area = 4 * 3.14159265 * R * R;
                fprintf(stderr, "  planet: absorbed - emitted = %+.1f W/m2 (%+.2f PW); a steady\n"
                                "  state is zero, and anything else is a term that is not conserving\n",
                        pw * 1e15 / area, pw);
                // Every term's global mean. Each transport term should sum
                // to zero over the planet; the one that does not is the leak.
                double g[NZ] = {0}, wsum = 0;
                for (int y = 0; y < AH; y++) {
                    double cw = std::cos(((y + 0.5) / (double)AH - 0.5) * 3.14159265);
                    const double* z = &c.zonBud[NZ * y];
                    for (int k = 0; k < NZ - 1; k++) g[k] += z[k] * cw;
                    wsum += cw;
                }
                for (int k = 0; k < NZ - 1; k++) g[k] /= wsum;
                fprintf(stderr, "  global means: TOA net %+.1f  surface net %+.1f  |  BLhor %+.1f  FThor %+.1f\n"
                                "    depos %+.1f  entr %+.1f  pool %+.1f  (vertical sum %+.1f)  cond-latent %+.1f\n",
                        g[0] - g[1], g[2] + g[3] - g[4] - g[5] - g[6], g[7], g[8], g[9], g[10],
                        g[11], g[9] + g[10] + g[11], g[12] - g[6]);
            }
            fprintf(stderr, "  (PW is the northward transport across the band's poleward edge;\n"
                            "   e-columns are Earth. BLhor/FThor: horizontal heat into each layer.\n"
                            "   depos: subsiding air into the BL; entr: BL air detrained into the FT;\n"
                            "   pool: the upper branch's exchange; cond: latent heat released aloft.)\n");
            // The same, by season, for the north: winter is where the
            // calibration misses by 20 K, and an annual mean hides it.
            static const char* SN[4] = {"DJF", "MAM", "JJA", "SON"};
            for (int se : {0, 2}) {
                fprintf(stderr, "\n  NORTH OF 45, %s (W/m2)\n", SN[se]);
                fprintf(stderr, "  %5s | %6s %6s %6s | %6s %6s %6s %6s %6s | %6s | %6s %6s %6s %6s %6s %6s | %6s %6s\n",
                        "lat", "absSW", "OLR", "net", "BLhor", "FThor", "depos", "entr", "pool",
                        "cond", "sSW", "LWdn", "LWup", "sens", "latent", "snet", "Tb", "Tf");
                for (int y0 = AH * 3 / 4; y0 < AH; y0 += 4) {
                    double s[NZ] = {0}; double n = 0, tb = 0, tf = 0;
                    double tSea = 0, nSea = 0, tLand = 0, nLand = 0, iceS = 0;
                    for (int y = y0; y < std::min(y0 + 4, AH); y++) {
                        const double* z = &c.zonBudS[(se * AH + y) * NZ];
                        for (int k = 0; k < NZ - 1; k++) s[k] += z[k];
                        for (int x = 0; x < AW; x++) {
                            int j = se * AW * AH + y * AW + x;
                            tb += c.airT[j];
                            tf += c.airTf[j];
                            if (c.elev[y * AW + x] <= 0.0f) { tSea += c.meanT[j]; nSea += 1; iceS += c.iceM[j]; }
                            else { tLand += c.meanT[j]; nLand += 1; }
                        }
                        n += 1;
                    }
                    for (int k = 0; k < NZ - 1; k++) s[k] /= n;
                    tb /= n * AW; tf /= n * AW;
                    double lat = ((y0 + 2.0) / AH - 0.5) * 180.0;
                    fprintf(stderr, "  %5.0f | %6.0f %6.0f %6.0f | %6.0f %6.0f %6.0f %6.0f %6.0f | %6.0f | %6.0f %6.0f %6.0f %6.0f %6.0f %6.0f | %6.1f %6.1f | sea %5.1f land %5.1f ice %4.2f m\n",
                            lat, s[0], s[1], s[0] - s[1], s[7], s[8], s[9], s[10], s[11], s[12],
                            s[2], s[3], s[4], s[5], s[6], s[2] + s[3] - s[4] - s[5] - s[6], tb, tf,
                            nSea > 0 ? tSea / nSea : 0.0, nLand > 0 ? tLand / nLand : 0.0,
                            nSea > 0 ? iceS / nSea : 0.0);
                }
            }
            // THE TWO-LEVEL DYNAMICS, judged on wind and pressure: zonal means
            // of both levels' zonal wind, the surface pressure, its standing
            // deviation in time (where the weather is), and the eddy kinetic
            // energy of the lower level against its zonal mean. Earth: surface
            // westerlies 5-8 m/s at 45-55, the jet 25-35 m/s aloft at 30-40,
            // trades -5 to -7, pressure deviation 8-12 hPa on the storm tracks
            // and 2-3 in the tropics, EKE 30-60 m2/s2 on the storm tracks.
            if (atmosphere::DYN2 && !c.d2u1.empty()) {
                fprintf(stderr, "\nTWO-LEVEL DYNAMICS (annual, zonal means)\n");
                fprintf(stderr, "  %5s %7s %7s %8s %7s %7s\n", "lat", "u low", "u up", "ps hPa", "sd hPa", "EKE");
                for (int y0 = 1; y0 < AH - 1; y0 += 4) {
                    double u1 = 0, u2 = 0, ps = 0, sd = 0, ek = 0, n = 0;
                    for (int y = y0; y < std::min(y0 + 4, AH - 1); y++)
                        for (int x = 0; x < AW; x++) {
                            int i = y * AW + x;
                            u1 += c.d2u1[i]; u2 += c.d2u2[i]; ps += c.d2ps[i]; sd += c.d2psSd[i]; ek += c.d2eke[i]; n += 1;
                        }
                    double lat = ((y0 + 2.0) / AH - 0.5) * 180.0;
                    fprintf(stderr, "  %5.0f %7.1f %7.1f %8.1f %7.1f %7.1f\n", lat, u1 / n, u2 / n, ps / n / 100.0, sd / n / 100.0, ek / n);
                }
                // and the pictures: mean pressure against its zonal mean (red
                // high, blue low, +-15 hPa), and the pressure's deviation
                // (white = 15 hPa)
                std::vector<unsigned char> pimg(AW * AH * 3, 0), simg(AW * AH * 3, 0);
                for (int y = 0; y < AH; y++) {
                    double zm = 0;
                    for (int x = 0; x < AW; x++) zm += c.d2ps[y * AW + x] / AW;
                    for (int x = 0; x < AW; x++) {
                        int i = y * AW + x, o = ((AH - 1 - y) * AW + x) * 3;
                        double a = (c.d2ps[i] - zm) / 1500.0; // +-1 at 15 hPa
                        pimg[o] = (unsigned char)std::clamp(128 + a * 127, 0.0, 255.0);
                        pimg[o + 1] = (unsigned char)(c.isWater[i] ? 100 : 128);
                        pimg[o + 2] = (unsigned char)std::clamp(128 - a * 127, 0.0, 255.0);
                        unsigned char g = (unsigned char)std::clamp(c.d2psSd[i] / 1500.0 * 255.0, 0.0, 255.0);
                        simg[o] = g; simg[o + 1] = g; simg[o + 2] = (unsigned char)(c.isWater[i] ? std::min(255, g + 50) : g);
                    }
                }
                char nm[64];
                snprintf(nm, sizeof nm, "dynps_seed%s.ppm", tag.c_str());
                if (FILE* f = fopen(nm, "wb")) { fprintf(f, "P6\n%d %d\n255\n", AW, AH); fwrite(pimg.data(), 1, pimg.size(), f); fclose(f); }
                snprintf(nm, sizeof nm, "dynsd_seed%s.ppm", tag.c_str());
                if (FILE* f = fopen(nm, "wb")) { fprintf(f, "P6\n%d %d\n255\n", AW, AH); fwrite(simg.data(), 1, simg.size(), f); fclose(f); }
            }
            // THE WORLD REVIEW, on the Earth template: named regions on every
            // continent with what Earth measures there -- annual rain in
            // mm/day, January and July surface temperature, and the biome --
            // against what the model makes. The point is not to fit Earth
            // but to find which rules hold and which fail, so that what
            // survives can be trusted on any world. Earth figures are
            // climatological means, rounded; the model's biome is the
            // classifier's dominant cover at the region's mean fields.
            if (earth) {
                struct Region { const char* name; double lon0, lon1, lat0, lat1; double eRain, eJan, eJul; const char* eBiome; };
                static const Region REG[] = {
                    // North America
                    {"US Pacific NW", -125, -118, 42, 50, 3.5, 3, 17, "forest"},
                    {"California", -123, -118, 34, 40, 1.2, 10, 20, "shrub"},
                    {"Great Basin", -118, -110, 36, 42, 0.7, -1, 23, "steppe/desert"},
                    {"Great Plains", -104, -95, 35, 50, 1.5, -6, 24, "grass"},
                    {"US Midwest", -95, -82, 38, 47, 2.5, -5, 23, "forest"},
                    {"US Southeast", -92, -78, 30, 36, 3.5, 8, 27, "forest"},
                    {"Canada boreal", -115, -80, 52, 62, 1.2, -20, 16, "taiga"},
                    {"Alaska int", -155, -145, 62, 67, 0.8, -22, 15, "taiga"},
                    {"Mexico plateau", -106, -100, 20, 28, 1.2, 12, 24, "steppe"},
                    // South America
                    {"Amazon", -70, -50, -10, 2, 6.0, 26, 25, "rainforest"},
                    {"NE Brazil", -45, -37, -12, -4, 2.0, 27, 25, "savanna"},
                    {"Pampas", -62, -57, -38, -30, 2.5, 23, 9, "grass"},
                    {"Patagonia", -72, -66, -50, -40, 0.6, 14, 2, "steppe"},
                    {"Peru coast", -80, -72, -25, -6, 0.1, 22, 16, "desert"},
                    // Europe
                    {"W Europe", -5, 15, 44, 54, 2.2, 3, 18, "forest"},
                    {"Spain", -8, 0, 37, 43, 1.5, 6, 25, "shrub"},
                    {"E Europe", 25, 45, 48, 56, 1.6, -6, 19, "forest"},
                    {"Scandinavia", 5, 25, 58, 66, 2.0, -6, 15, "taiga"},
                    // Asia
                    {"C Siberia", 90, 120, 55, 65, 1.2, -30, 17, "taiga"},
                    {"Arabia", 42, 55, 18, 28, 0.2, 15, 35, "desert"},
                    {"Iran", 50, 60, 28, 36, 0.6, 4, 28, "desert"},
                    {"C Asia", 60, 75, 38, 46, 0.6, -5, 26, "steppe/desert"},
                    {"India", 74, 84, 18, 26, 3.0, 20, 29, "savanna"},
                    {"N China", 105, 120, 32, 40, 1.8, -2, 26, "forest"},
                    {"S China", 105, 120, 22, 30, 4.5, 8, 28, "forest"},
                    {"Mongolia", 95, 115, 42, 50, 0.6, -20, 18, "steppe"},
                    {"SE Asia", 98, 108, 10, 20, 5.0, 24, 28, "rainforest"},
                    {"Japan", 130, 142, 32, 40, 4.5, 4, 25, "forest"},
                    // Africa
                    {"NW Africa", -10, 5, 30, 36, 1.2, 10, 27, "shrub"},
                    {"Sahara", -5, 25, 18, 28, 0.1, 13, 33, "desert"},
                    {"Sahel", -10, 20, 10, 17, 1.5, 24, 29, "savanna"},
                    {"Congo", 15, 28, -5, 3, 5.0, 25, 24, "rainforest"},
                    {"E Africa", 34, 40, -5, 5, 2.0, 21, 19, "savanna"},
                    {"S Africa", 18, 30, -32, -22, 1.4, 22, 11, "steppe"},
                    // Australia
                    {"Australia int", 125, 140, -30, -22, 0.6, 29, 13, "desert"},
                    {"E Australia", 145, 153, -35, -25, 2.5, 24, 12, "forest"},
                    {"N Australia", 125, 140, -18, -12, 2.5, 29, 24, "savanna"},
                };
                static const char* COVN[11] = {"bare", "tundra", "taiga", "forest", "rainforest", "grass",
                                               "steppe", "savanna", "shrub", "marsh", "desert"};
                fprintf(stderr, "\nTHE WORLD REVIEW (Earth template; model vs Earth; rain mm/day, T degC)\n");
                fprintf(stderr, "  %-15s %5s %5s %5s %5s %5s | %5s %5s | %5s %5s | %-11s %-13s\n",
                        "region", "rain", "earth", "DJF", "JJA", "evap", "Tjan", "earth", "Tjul", "earth", "model cover", "earth biome");
                int rainOk = 0, tOk = 0, bioOk = 0, nReg = 0;
                for (const Region& r : REG) {
                    double rn[4] = {0}, ev = 0, tj = 0, tl = 0, t = 0, n = 0, el = 0;
                    for (int y = 0; y < AH; y++) {
                        double lat = ((y + 0.5) / AH - 0.5) * 180.0;
                        if (lat < r.lat0 || lat > r.lat1) continue;
                        for (int x = 0; x < AW; x++) {
                            double lon = ((x + 0.5) / AW) * 360.0 - 180.0;
                            if (lon < r.lon0 || lon > r.lon1) continue;
                            int i = y * AW + x;
                            if (c.isWater[i]) continue;
                            for (int se = 0; se < 4; se++) {
                                int j = se * AW * AH + i;
                                rn[se] += c.rainMmDay[j]; ev += c.evapF[j] / 4; t += c.meanT[j] / 4;
                            }
                            // January is the middle of DJF, July of JJA
                            tj += c.meanT[0 * AW * AH + i]; tl += c.meanT[2 * AW * AH + i];
                            el += c.elev[i];
                            n += 1;
                        }
                    }
                    if (n < 1) continue;
                    double rain = (rn[0] + rn[1] + rn[2] + rn[3]) / 4 / n;
                    double pet = std::max(0.4, 0.11 * (t / n + 8.0));
                    float mo = (float)std::clamp(0.5 * rain / pet, 0.0, 1.0);
                    // January is DJF in both hemispheres; the Earth figures are
                    // January and July, not summer and winter.
                    double jan = tj / n, jul = tl / n;
                    (void)mo; (void)el;
                    // The model's biome: the most common dominant cover over
                    // the box's cells, each classified on its own fields (a
                    // box mean would turn one wet corner into a forest).
                    int best = 0;
                    {
                        int votes[11] = {0};
                        for (int y = 0; y < AH; y++) {
                            double lat = ((y + 0.5) / AH - 0.5) * 180.0;
                            if (lat < r.lat0 || lat > r.lat1) continue;
                            for (int x = 0; x < AW; x++) {
                                double lon = ((x + 0.5) / AW) * 360.0 - 180.0;
                                if (lon < r.lon0 || lon > r.lon1) continue;
                                int i = y * AW + x;
                                if (c.isWater[i]) continue;
                                double rr = 0, tt = 0, tc = 1e9, tw = -1e9;
                                for (int se = 0; se < 4; se++) {
                                    int j = se * AW * AH + i;
                                    rr += c.rainMmDay[j] / 4; tt += c.meanT[j] / 4;
                                    tc = std::min(tc, (double)c.meanT[j]); tw = std::max(tw, (double)c.meanT[j]);
                                }
                                double pp = std::max(0.4, 0.11 * (tt + 8.0));
                                float mm = (float)std::clamp(0.5 * rr / pp, 0.0, 1.0);
                                terrain::Mixture mx = terrain::mixtureAt(c.elev[i], 0.0f, (float)tt, mm, 0.0f, false,
                                                                         0.5f, 0.0f, (float)tc, (float)tw);
                                int b2 = 0;
                                for (int k = 1; k < 11; k++) if (mx.cov[k] > mx.cov[b2]) b2 = k;
                                votes[b2]++;
                            }
                        }
                        for (int k = 1; k < 11; k++) if (votes[k] > votes[best]) best = k;
                    }
                    bool rOk = rain > r.eRain / 1.6 && rain < r.eRain * 1.6 && std::fabs(rain - r.eRain) < 1.0 + 0.6 * r.eRain;
                    if (r.eRain < 0.3) rOk = rain < 0.5;
                    bool tOkHere = std::fabs(jan - r.eJan) <= 3.0 && std::fabs(jul - r.eJul) <= 3.0;
                    bool bOk = std::string(r.eBiome).find(COVN[best]) != std::string::npos;
                    rainOk += rOk; tOk += tOkHere; bioOk += bOk; nReg++;
                    fprintf(stderr, "  %-15s %5.1f %5.1f %5.1f %5.1f %5.1f | %5.0f %5.0f | %5.0f %5.0f | %-11s %-13s %s%s%s\n",
                            r.name, rain, r.eRain, rn[0] / n, rn[2] / n, ev / n, jan, r.eJan, jul, r.eJul,
                            COVN[best], r.eBiome, rOk ? "" : "R", tOkHere ? "" : "T", bOk ? "" : "B");
                }
                // TRANSECTS: where the water goes, cell by cell along a row.
                // Annual means: land flag, elevation, column water, its
                // saturation (against the lift-lowered ceiling), rain, evap,
                // and the zonal wind.
                struct Tr { const char* name; double lat, lon0, lon1; };
                static const Tr TRS[] = {{"47N, Pacific to Atlantic", 47, -140, -60},
                                         {"40N, Atlantic to China", 40, -20, 120},
                                         {"20S, Pacific to Atlantic", -20, -85, -35}};
                for (const Tr& tr : TRS) {
                    int y = std::clamp((int)((tr.lat + 90.0) / 180.0 * AH), 0, AH - 1);
                    fprintf(stderr, "\n  TRANSECT %s (row lat %.1f)\n", tr.name, ((y + 0.5) / AH - 0.5) * 180.0);
                    fprintf(stderr, "  %7s %2s %5s %6s %5s %6s %6s %6s\n", "lon", "L", "elev", "Wv mm", "sat", "rain", "evap", "u");
                    for (int x = 0; x < AW; x++) {
                        double lon = ((x + 0.5) / AW) * 360.0 - 180.0;
                        if (lon < tr.lon0 || lon > tr.lon1) continue;
                        int i = y * AW + x;
                        double wv = 0, rh = 0, rn = 0, ev = 0, uu = 0;
                        for (int se = 0; se < 4; se++) {
                            int j = se * AW * AH + i;
                            wv += c.wv[j] / 4; rh += c.rh[j] / 4; rn += c.rainMmDay[j] / 4; ev += c.evapF[j] / 4; uu += c.windU[j] / 4;
                        }
                        fprintf(stderr, "  %7.1f %2s %5.0f %6.1f %5.2f %6.2f %6.2f %6.1f\n", lon,
                                c.isWater[i] ? "~" : "L", c.elev[i], wv, rh, rn, ev, uu);
                    }
                }
                fprintf(stderr, "  of %d regions: rain within reason %d, temperatures within 3 K %d, biome right %d\n"
                                "  (flags: R rain off, T temperature off, B biome wrong; a region's evap is its own land's)\n",
                        nReg, rainOk, tOk, bioOk);
            }
            // And the winter north split by surface: the sea and the land
            // at one latitude are different animals, and the mean of them
            // says which is warm without saying why.
            for (int sf = 1; sf >= 0; sf--) {
                fprintf(stderr, "\n  NORTH OF 45, DJF, %s ONLY (W/m2; cells by the atmosphere's own mask)\n",
                        sf ? "LAND" : "SEA");
                fprintf(stderr, "  %5s | %6s %6s %6s | %6s %6s %6s %6s %6s | %6s | %6s %6s %6s %6s %6s %6s | %6s %6s %6s\n",
                        "lat", "absSW", "OLR", "net", "BLhor", "FThor", "depos", "entr", "pool",
                        "cond", "sSW", "LWdn", "LWup", "sens", "latent", "snet", "Ts", "Tb", "Tf");
                for (int y0 = AH * 3 / 4; y0 < AH; y0 += 4) {
                    double s[NZ] = {0}; double n = 0, ts = 0, tb = 0, tf = 0, nc = 0;
                    for (int y = y0; y < std::min(y0 + 4, AH); y++) {
                        const double* z = &c.zonBudLS[((0 * 2 + sf) * AH + y) * NZ];
                        double w = z[NZ - 1];
                        for (int k = 0; k < NZ - 1; k++) s[k] += z[k] * w;
                        n += w;
                        for (int x = 0; x < AW; x++) {
                            bool land = c.elev[y * AW + x] > 0.0f;
                            if (land != (sf == 1)) continue;
                            int j = y * AW + x;
                            ts += c.meanT[j]; tb += c.airT[j]; tf += c.airTf[j]; nc += 1;
                        }
                    }
                    if (n < 1) continue;
                    for (int k = 0; k < NZ - 1; k++) s[k] /= n;
                    double lat = ((y0 + 2.0) / AH - 0.5) * 180.0;
                    fprintf(stderr, "  %5.0f | %6.0f %6.0f %6.0f | %6.0f %6.0f %6.0f %6.0f %6.0f | %6.0f | %6.0f %6.0f %6.0f %6.0f %6.0f %6.0f | %6.1f %6.1f %6.1f\n",
                            lat, s[0], s[1], s[0] - s[1], s[7], s[8], s[9], s[10], s[11], s[12],
                            s[2], s[3], s[4], s[5], s[6], s[2] + s[3] - s[4] - s[5] - s[6],
                            nc > 0 ? ts / nc : 0.0, nc > 0 ? tb / nc : 0.0, nc > 0 ? tf / nc : 0.0);
                }
            }
            fprintf(stderr, "\n  SURFACE, zonal: %5s %6s %6s %6s %6s %6s %6s\n", "lat", "SW", "LWdn",
                    "LWup", "sens", "latent", "net");
            for (int y0 = 0; y0 < AH; y0 += 8) {
                double s[NZ] = {0}; double n = 0;
                for (int y = y0; y < std::min(y0 + 8, AH); y++) {
                    const double* z = &c.zonBud[NZ * y];
                    for (int k = 0; k < NZ - 1; k++) s[k] += z[k];
                    n += 1;
                }
                for (int k = 0; k < NZ - 1; k++) s[k] /= n;
                double lat = ((y0 + 4.0) / AH - 0.5) * 180.0;
                fprintf(stderr, "                  %5.0f %6.0f %6.0f %6.0f %6.0f %6.0f %6.0f\n", lat,
                        s[2], s[3], s[4], s[5], s[6], s[2] + s[3] - s[4] - s[5] - s[6]);
            }
        }

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

        // The equator takes 0.84 mm/day and 38 degrees takes 2.28, which is the
        // rainfall profile upside down. Two things could do that and they live
        // in different code: either the air never rises over the equator, or it
        // rises and no rain follows. The model already records what lifts it,
        // so ask. Everything here is a zonal mean over the whole band, land and
        // sea alike -- the ITCZ is an ocean feature first.
        {
            fprintf(stderr, "\nWHAT LIFTS THE AIR, AND WHAT FALLS OUT (zonal, all surfaces)\n");
            fprintf(stderr, "  %5s %9s %9s %9s %9s %9s %8s %7s\n", "lat", "convect", "converge",
                    "front", "orog", "total", "Wv mm", "rain");
            for (int y0 = 0; y0 < AH; y0 += 6) {
                double uc = 0, ud = 0, uf = 0, uo = 0, wv = 0, rn = 0, n = 0;
                for (int y = y0; y < std::min(y0 + 6, AH); y++)
                    for (int x = 0; x < AW; x++)
                        for (int se = 0; se < atmosphere::SEASONS; se++) {
                            int i = se * AW * AH + y * AW + x;
                            uc += c.upConv[i]; ud += c.upDiv[i];
                            uf += c.upFront[i]; uo += c.upOrog[i];
                            wv += c.wv[i]; rn += c.rainMmDay[i];
                            n += 1;
                        }
                if (n < 1) continue;
                double lat = ((y0 + 3.0) / AH - 0.5) * 180.0;
                fprintf(stderr, "  %5.0f %9.4f %9.4f %9.4f %9.4f %9.4f %8.1f %7.2f\n", lat,
                        uc / n, ud / n, uf / n, uo / n, (uc + ud + uf + uo) / n, wv / n, rn / n);
            }
            fprintf(stderr, "  (ascent in m/s; life's ITCZ rises at roughly 0.005 to 0.01)\n");

            // In a steady state rain EQUALS evaporation, so no amount of uplift
            // can raise the total -- it can only move it about. If the tropics
            // are dry it is because the cycle itself is running slow, and the
            // question is which term holds it back: the wind that carries the
            // vapour off the surface, the surface's own supply of water, or the
            // sunlight available to pay the latent heat.
            fprintf(stderr, "\nWHY THE CYCLE RUNS SLOW (zonal, all surfaces)\n");
            fprintf(stderr, "  %5s %8s %8s %8s %8s %9s\n", "lat", "evap", "rain", "wind",
                    "supply", "energy-capped");
            for (int y0 = 0; y0 < AH; y0 += 6) {
                double ev = 0, rn = 0, sp = 0, su = 0, af = 0, n = 0;
                for (int y = y0; y < std::min(y0 + 6, AH); y++)
                    for (int x = 0; x < AW; x++)
                        for (int se = 0; se < atmosphere::SEASONS; se++) {
                            int i = se * AW * AH + y * AW + x;
                            ev += c.evapF[i]; rn += c.rainMmDay[i];
                            sp += c.spdF[i]; su += c.supplyF[i]; af += c.affordF[i];
                            n += 1;
                        }
                if (n < 1) continue;
                double lat = ((y0 + 3.0) / AH - 0.5) * 180.0;
                fprintf(stderr, "  %5.0f %8.2f %8.2f %8.2f %8.2f %8.0f%%\n", lat, ev / n, rn / n,
                        sp / n, su / n, 100 * af / n);
            }
            fprintf(stderr, "  (life: about 2.7 mm/day globally, and 4 to 5 over tropical ocean;\n"
                            "   near-surface wind over the sea runs 6 to 7 m/s)\n");
            // Rain must equal evaporation in a steady state. dbgEvap divides by
            // the spin-up hours as well as the sampled ones, so it is diluted;
            // these two fields share a normalisation, so they can be compared.
            // If they disagree, water is going somewhere.
            {
                double ev = 0, rn = 0, wgt = 0;
                for (int y = 0; y < AH; y++) {
                    double cwl = std::cos(((y + 0.5) / AH - 0.5) * 3.14159265);
                    for (int x = 0; x < AW; x++)
                        for (int se = 0; se < atmosphere::SEASONS; se++) {
                            int i = se * AW * AH + y * AW + x;
                            ev += c.evapF[i] * cwl;
                            rn += c.rainMmDay[i] * cwl;
                            wgt += cwl;
                        }
                }
                fprintf(stderr,
                        "  GLOBAL, same normalisation: evap %.2f  rain %.2f  gap %+.2f mm/day\n",
                        ev / wgt, rn / wgt, ev / wgt - rn / wgt);
                // Advection and diffusion only MOVE water, so summed over the
                // whole planet they must come to nothing. If they do not, the
                // transport is inventing or destroying the gap.
                double ad = 0, di = 0, az = 0, am = 0;
                for (int y = 0; y < AH; y++) {
                    double cwl = std::cos(((y + 0.5) / AH - 0.5) * 3.14159265);
                    for (int x = 0; x < AW; x++)
                        for (int se = 0; se < atmosphere::SEASONS; se++) {
                            int i = se * AW * AH + y * AW + x;
                            ad += c.advF[i] * cwl; di += c.difF[i] * cwl;
                            az += c.advZF[i] * cwl; am += c.advMF[i] * cwl;
                        }
                }
                fprintf(stderr,
                        "  transport, which should sum to zero: adv %+.3f  dif %+.3f"
                        "  (zonal %+.3f, meridional %+.3f)\n",
                        ad / wgt, di / wgt, az / wgt, am / wgt);
            }
        }

        // Everything above runs on the 192x96 atmosphere grid at the block-mean
        // elevation, which means no lapse correction at all -- hLocal and the
        // coarse elevation are the same number, so the term vanishes. The
        // renderer does not do that. It calls deriveAt per pixel at the TRUE
        // local height, and the lapse term there is worth several degrees in
        // both directions inside a single 208 km cell. Sand needs ground above
        // 2 C, so a report of sand near a pole cannot come from the coarse
        // grid and has to be looked for the way the map draws it.
        {
            const int GW = hydrology::W, GH = hydrology::H;
            const int NB = 12;
            double sSand[NB] = {0}, sDes[NB] = {0}, sBare[NB] = {0}, sN[NB] = {0};
            double sIce[NB] = {0}, sSoil[NB] = {0}, sWarm[NB] = {0};
            double sT[NB] = {0}, sM[NB] = {0}, hot = 0, hotN = 0;
            double ghost = 0, ghostSand = 0, ghostWarm = 0, ghostPolar = 0;
            double bleed = 0, bleedN = 0, bleedBig = 0, bleedGate = 0;
            double zBleed[12] = {0}, zBleedN[12] = {0};
            #pragma omp parallel for
            for (int y = 0; y < GH; y++) {
                double lat = ((y + 0.5) / GH - 0.5) * 3.14159265;
                // Accumulate the row privately and merge once, not per cell:
                // a critical section two million times over is not a probe.
                double rSand = 0, rDes = 0, rBare = 0, rT = 0, rM = 0, rN = 0, rHot = 0, rHotN = 0;
                double rGhost = 0, rGhostSand = 0, rGhostWarm = 0, rGhostPolar = 0;
                double rBleed = 0, rBleedN = 0, rBleedBig = 0, rBleedGate = 0;
                double rIce = 0, rSoil = 0, rWarm = 0;
                for (int x = 0; x < GW; x += 2) {
                    float h = hy.heightM[y * GW + x];
                    if (h <= 0) continue;
                    double lon = ((x + 0.5) / GW * 2.0 - 1.0) * 3.14159265;
                    terrain::V3 n{(float)(std::cos(lat) * std::cos(lon)),
                                  (float)(std::cos(lat) * std::sin(lon)), (float)std::sin(lat)};
                    terrain::V3 w = terrain::rotate(rot, n) + offset;
                    atmosphere::DerivedClimate d =
                        atmosphere::deriveAt(c, (float)lat, (float)lon, w, h);
                    float slope = terrain::slopeAt(n, cp, seaLevel, 8, pf, rot, offset);
                    float up = pf.sample({n.x, n.y, n.z}).uplift;
                    terrain::Mixture m =
                        terrain::mixtureAt(h, slope, d.temp, d.moist, up, false,
                                           terrain::patchNoise(w), d.swamp, d.tCold, d.tWarm);
                    rSand += m.sub[1]; rDes += m.cov[10]; rBare += m.cov[0];
                    rIce += m.sub[6]; rSoil += m.sub[0]; rWarm += d.tWarm;
                    rT += d.temp; rM += d.moist; rN += 1;
                    if (m.sub[1] > 0.4) { rHot += d.temp; rHotN += 1; }
                    // The climate grid is 208 km to a cell, and a cell that is
                    // less than half land is called ocean and carries the sea's
                    // climate. Polar sea in this model runs 7 K warmer than
                    // polar land. So ask directly: how much land lives inside a
                    // cell the atmosphere calls ocean, and does it come out
                    // above the 2 C the sand gate needs?
                    // How much warmth does the land borrow from the sea? Same
                    // fuzzed point, same bilinear, once as the model does it
                    // and once masked to land cells only.
                    {
                        terrain::V3 nf = atmosphere::climFuzz(
                            atmosphere::unitAt((float)lat, (float)lon));
                        bool anyLand = false;
                        double raw = 0, msk = 0;
                        for (int se = 0; se < atmosphere::SEASONS; se++) {
                            raw += atmosphere::bilinearAt(c.meanT, se, nf) / atmosphere::SEASONS;
                            msk += landMaskedT(c, se, nf, anyLand) / atmosphere::SEASONS;
                        }
                        if (anyLand) {
                            rBleed += raw - msk;
                            rBleedN += 1;
                            if (raw - msk > 2.0) rBleedBig += 1;
                            // Does the borrowed warmth carry it over the gate?
                            if (raw > 2.0 && msk <= 2.0) rBleedGate += 1;
                        }
                    }
                    int ax = std::min(AW - 1, std::max(0, (int)((lon / 6.2831853 + 0.5) * AW)));
                    int ay = std::min(AH - 1, std::max(0, (int)((lat / 3.14159265 + 0.5) * AH)));
                    if (c.elev[ay * AW + ax] <= 0.0f) {
                        rGhost += 1;
                        rGhostSand += m.sub[1];
                        if (d.temp > 2.0f) rGhostWarm += 1;
                        if (std::fabs(lat) > 0.87) rGhostPolar += 1; // beyond 50 degrees
                    }
                }
                if (rN < 1) continue;
                int b = std::min(NB - 1, std::max(0, (int)((lat / 3.14159265 + 0.5) * NB)));
                #pragma omp critical
                {
                    sSand[b] += rSand; sDes[b] += rDes; sBare[b] += rBare;
                    sT[b] += rT; sM[b] += rM; sN[b] += rN;
                    sIce[b] += rIce; sSoil[b] += rSoil; sWarm[b] += rWarm;
                    hot += rHot; hotN += rHotN;
                    bleed += rBleed; bleedN += rBleedN;
                    bleedBig += rBleedBig; bleedGate += rBleedGate;
                    zBleed[b] += rBleed; zBleedN[b] += rBleedN;
                    ghost += rGhost; ghostSand += rGhostSand;
                    ghostWarm += rGhostWarm; ghostPolar += rGhostPolar;
                }
            }
            fprintf(stderr, "\nAS THE MAP DRAWS IT: full resolution, deriveAt, real slope\n");
            fprintf(stderr, "    %5s %8s %8s %8s %8s %8s %8s %8s\n", "lat", "sand", "desert",
                    "bare", "ice", "soil", "summer", "degC");
            double tot = 0, tS = 0, tD = 0, tB = 0;
            for (int b = 0; b < NB; b++) {
                if (sN[b] < 1) continue;
                double la = ((b + 0.5) / NB - 0.5) * 180.0;
                fprintf(stderr, "    %5.0f %7.1f%% %7.1f%% %7.1f%% %7.1f%% %7.1f%% %8.1f %8.1f\n",
                        la, 100 * sSand[b] / sN[b], 100 * sDes[b] / sN[b], 100 * sBare[b] / sN[b],
                        100 * sIce[b] / sN[b], 100 * sSoil[b] / sN[b], sWarm[b] / sN[b],
                        sT[b] / sN[b]);
                tot += sN[b]; tS += sSand[b]; tD += sDes[b]; tB += sBare[b];
            }
            fprintf(stderr, "    %5s %7.1f%% %7.1f%% %7.1f%%   (mean annual temp where sand > 40%%: %.1f C)\n",
                    "all", 100 * tS / std::max(tot, 1.0), 100 * tD / std::max(tot, 1.0),
                    100 * tB / std::max(tot, 1.0), hot / std::max(hotN, 1.0));
            fprintf(stderr,
                    "  land inside cells the atmosphere calls ocean: %.1f%% of all land\n"
                    "    of it, %.1f%% reads above the 2 C sand needs, and its mean sand is %.0f%%\n"
                    "    %.1f%% of it lies beyond 50 degrees\n"
                    "  WARMTH THE LAND BORROWS FROM THE SEA (bilinear vs land-masked)\n"
                    "    mean over land %+.2f K\n"
                    "    land warmed by more than 2 K: %.1f%%\n"
                    "    land carried OVER the 2 C sand gate by it: %.1f%%\n",
                    100 * ghost / std::max(tot, 1.0), 100 * ghostWarm / std::max(ghost, 1.0),
                    100 * ghostSand / std::max(ghost, 1.0),
                    100 * ghostPolar / std::max(ghost, 1.0),
                    bleed / std::max(bleedN, 1.0), 100 * bleedBig / std::max(bleedN, 1.0),
                    100 * bleedGate / std::max(bleedN, 1.0));
            fprintf(stderr, "    by latitude:");
            for (int b = 0; b < NB; b++)
                if (zBleedN[b] > 0)
                    fprintf(stderr, " %.0f:%+.1f", ((b + 0.5) / NB - 0.5) * 180.0,
                            zBleed[b] / zBleedN[b]);
            fprintf(stderr, "\n");

            // The cursor reads 0.0 mm/d wherever it is put down. A zonal mean
            // of 0.84 is consistent both with land that drizzles everywhere
            // and with land that is bone dry except for a few soaked cells,
            // and those are different bugs. So count the cells, by season,
            // since the readout is seasonal and the moisture is annual.
            double hist[6] = {0}, nLand = 0, zeroAny = 0, zeroAll = 0;
            const double EDGE[6] = {0.05, 0.5, 1.0, 2.0, 5.0, 1e9};
            for (int i = 0; i < AW * AH; i++) {
                if (c.elev[i] <= 0.0f) continue;
                double ann = 0;
                int nz = 0;
                for (int se = 0; se < atmosphere::SEASONS; se++) {
                    double r = c.rainMmDay[se * AW * AH + i];
                    ann += r / atmosphere::SEASONS;
                    if (r < 0.05) nz++;
                }
                for (int b = 0; b < 6; b++)
                    if (ann < EDGE[b]) { hist[b] += 1; break; }
                if (nz > 0) zeroAny += 1;
                if (nz == atmosphere::SEASONS) zeroAll += 1;
                nLand += 1;
            }
            fprintf(stderr, "\nHOW THE RAIN IS SPREAD OVER LAND (annual mean, mm/day)\n");
            const char* LBL[6] = {"  < 0.05", "0.05-0.5", " 0.5-1.0", " 1.0-2.0",
                                  " 2.0-5.0", "   > 5.0"};
            for (int b = 0; b < 6; b++)
                fprintf(stderr, "  %s %6.1f%%\n", LBL[b], 100 * hist[b] / std::max(nLand, 1.0));
            fprintf(stderr, "  land with a bone-dry season: %.1f%%   dry all four: %.1f%%\n",
                    100 * zeroAny / std::max(nLand, 1.0), 100 * zeroAll / std::max(nLand, 1.0));
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
            // Naming classes keeps answering the wrong question. Six of the
            // eleven covers are painted in a tan or khaki -- tundra above all,
            // at 0.55/0.52/0.42, which reads as sand from any distance -- so
            // the desert share says nothing about whether the map looks like a
            // desert. Run the shader's own palette and ask what colour the
            // land actually comes out. Green foliage is the only thing in the
            // palette with more green in it than red.
            {
                const double SUBC[7][3] = {{0.45, 0.38, 0.28}, {0.80, 0.72, 0.50},
                                           {0.45, 0.42, 0.38}, {0.52, 0.49, 0.45},
                                           {0.55, 0.48, 0.36}, {0.35, 0.33, 0.25},
                                           {0.94, 0.95, 0.97}};
                const double COVC[11][3] = {{0, 0, 0},          {0.55, 0.52, 0.42},
                                            {0.10, 0.26, 0.16}, {0.12, 0.32, 0.12},
                                            {0.06, 0.28, 0.10}, {0.36, 0.52, 0.22},
                                            {0.62, 0.58, 0.32}, {0.60, 0.56, 0.28},
                                            {0.50, 0.50, 0.30}, {0.25, 0.40, 0.25},
                                            {0.78, 0.66, 0.42}};
                double green = 0, rSum = 0, gSum = 0, bSum = 0, n2 = 0;
                double zG[12] = {0}, zN[12] = {0}, cG = 0, cN = 0, iG = 0, iN = 0;
                double zR[12] = {0}, zM[12] = {0}, zT[12] = {0};
                // And the picture itself, in the same palette, written as a
                // PPM next to the sweep's output: the sea blue, the land as
                // the shader would paint it, north up.
                std::vector<unsigned char> img(AW * AH * 3, 0);
                for (int i = 0; i < AW * AH; i++) {
                    if (c.elev[i] <= 0.0f) {
                        int x = i % AW, y = i / AW, o = ((AH - 1 - y) * AW + x) * 3;
                        img[o] = 30; img[o + 1] = 60; img[o + 2] = 110;
                        continue;
                    }
                    int x = i % AW, y = i / AW;
                    double lat = ((y + 0.5) / (double)AH - 0.5) * 3.14159265;
                    double lon = ((x + 0.5) / (double)AW * 2.0 - 1.0) * 3.14159265;
                    terrain::V3 nn{(float)(std::cos(lat) * std::cos(lon)),
                                   (float)(std::cos(lat) * std::sin(lon)), (float)std::sin(lat)};
                    terrain::V3 ww = terrain::rotate(rot, nn) + offset;
                    double rain = 0, t = 0, tc = 1e9, tw = -1e9;
                    for (int se = 0; se < atmosphere::SEASONS; se++) {
                        rain += c.rainMmDay[se * AW * AH + i] / atmosphere::SEASONS;
                        t += c.meanT[se * AW * AH + i] / atmosphere::SEASONS;
                        tc = std::min(tc, (double)c.meanT[se * AW * AH + i]);
                        tw = std::max(tw, (double)c.meanT[se * AW * AH + i]);
                    }
                    double pet = std::max(0.4, 0.11 * (t + 8.0));
                    float mo = (float)std::clamp(0.5 * rain / pet, 0.0, 1.0) +
                               terrain::moistureDetail(ww);
                    terrain::Mixture mx = terrain::mixtureAt(
                        c.elev[i], 0.0f, (float)t, std::clamp(mo, 0.0f, 1.0f), 0.0f, false,
                        terrain::patchNoise(ww), 0.0f, (float)tc, (float)tw);
                    double col[3] = {0, 0, 0};
                    for (int k = 0; k < 3; k++) {
                        double base = 0;
                        for (int j = 0; j < 7; j++) base += SUBC[j][k] * mx.sub[j];
                        col[k] = base * mx.cov[0];
                        for (int j = 1; j < 11; j++) col[k] += COVC[j][k] * mx.cov[j];
                    }
                    rSum += col[0]; gSum += col[1]; bSum += col[2];
                    if (col[1] > col[0]) green += 1;
                    n2 += 1;
                    {
                        int o = ((AH - 1 - y) * AW + x) * 3;
                        for (int k = 0; k < 3; k++)
                            img[o + k] = (unsigned char)std::clamp(col[k] * 255.0, 0.0, 255.0);
                    }
                    // Where the green is, not how much: a world with the right
                    // green share can still be green in all the wrong places.
                    int band = std::min(11, std::max(0, y * 12 / AH));
                    zN[band] += 1;
                    if (col[1] > col[0]) zG[band] += 1;
                    zR[band] += rain;
                    zM[band] += mo;
                    zT[band] += t;
                    bool coastal = false;
                    for (int dy = -2; dy <= 2 && !coastal; dy++)
                        for (int dx = -2; dx <= 2 && !coastal; dx++) {
                            int yy = y + dy;
                            if (yy < 0 || yy >= AH) continue;
                            int xx = ((x + dx) % AW + AW) % AW;
                            if (c.elev[yy * AW + xx] <= 0.0f) coastal = true;
                        }
                    if (coastal) { cN += 1; if (col[1] > col[0]) cG += 1; }
                    else         { iN += 1; if (col[1] > col[0]) iG += 1; }
                }
                {
                    char name[64];
                    snprintf(name, sizeof name, "map_seed%s.ppm", tag.c_str());
                    if (FILE* f = fopen(name, "wb")) {
                        fprintf(f, "P6\n%d %d\n255\n", AW, AH);
                        fwrite(img.data(), 1, img.size(), f);
                        fclose(f);
                        fprintf(stderr, "\n  (the map, in the shader's palette: %s)\n", name);
                    }
                    // And the fields behind it: annual rain (dark = dry,
                    // 6 mm/day = white), and the annual wind (red = east,
                    // blue = west, green = north; sea darkened).
                    std::vector<unsigned char> rimg(AW * AH * 3, 0), wimg(AW * AH * 3, 0);
                    for (int i = 0; i < AW * AH; i++) {
                        int x = i % AW, y = i / AW, o = ((AH - 1 - y) * AW + x) * 3;
                        double rain = 0, uu = 0, vv = 0;
                        for (int se = 0; se < atmosphere::SEASONS; se++) {
                            rain += c.rainMmDay[se * AW * AH + i] / atmosphere::SEASONS;
                            uu += c.windU[se * AW * AH + i] / atmosphere::SEASONS;
                            vv += c.windV[se * AW * AH + i] / atmosphere::SEASONS;
                        }
                        bool sea = c.elev[i] <= 0.0f;
                        unsigned char g = (unsigned char)std::clamp(rain / 6.0 * 255.0, 0.0, 255.0);
                        rimg[o] = sea ? g / 2 : g; rimg[o + 1] = sea ? g / 2 : g; rimg[o + 2] = sea ? (unsigned char)std::min(255, g / 2 + 60) : g;
                        double sc = sea ? 0.5 : 1.0;
                        wimg[o] = (unsigned char)std::clamp((128 + uu * 12.0) * sc, 0.0, 255.0);
                        wimg[o + 1] = (unsigned char)std::clamp((128 + vv * 12.0) * sc, 0.0, 255.0);
                        wimg[o + 2] = (unsigned char)(sea ? 90 : 40);
                    }
                    {
                        // The two land masks: green where the climatology's
                        // elevation is above zero, red where the model's own
                        // mask says water; yellow is both, which is a cell
                        // that disagrees with itself.
                        std::vector<unsigned char> mimg(AW * AH * 3, 0);
                        for (int i = 0; i < AW * AH; i++) {
                            int x = i % AW, y = i / AW, o = ((AH - 1 - y) * AW + x) * 3;
                            mimg[o] = c.isWater[i] ? 200 : 0;
                            mimg[o + 1] = c.elev[i] > 0.0f ? 200 : 0;
                            mimg[o + 2] = 40;
                        }
                        snprintf(name, sizeof name, "mask_seed%s.ppm", tag.c_str());
                        if (FILE* f = fopen(name, "wb")) { fprintf(f, "P6\n%d %d\n255\n", AW, AH); fwrite(mimg.data(), 1, mimg.size(), f); fclose(f); }
                    }
                    snprintf(name, sizeof name, "rain_seed%s.ppm", tag.c_str());
                    if (FILE* f = fopen(name, "wb")) { fprintf(f, "P6\n%d %d\n255\n", AW, AH); fwrite(rimg.data(), 1, rimg.size(), f); fclose(f); }
                    snprintf(name, sizeof name, "wind_seed%s.ppm", tag.c_str());
                    if (FILE* f = fopen(name, "wb")) { fprintf(f, "P6\n%d %d\n255\n", AW, AH); fwrite(wimg.data(), 1, wimg.size(), f); fclose(f); }
                }
                fprintf(stderr, "\nWHAT COLOUR IS THE LAND, in the shader's own palette\n");
                fprintf(stderr, "  mean rendered land  %.2f %.2f %.2f\n", rSum / std::max(n2, 1.0),
                        gSum / std::max(n2, 1.0), bSum / std::max(n2, 1.0));
                fprintf(stderr, "  reads as green      %5.1f%%   life about 48%%\n",
                        100 * green / std::max(n2, 1.0));
                fprintf(stderr, "  reads as tan/bare   %5.1f%%   life about 52%%\n",
                        100 * (n2 - green) / std::max(n2, 1.0));
                fprintf(stderr, "  coastal green %5.1f%%   interior green %5.1f%%\n",
                        100 * cG / std::max(cN, 1.0), 100 * iG / std::max(iN, 1.0));
                fprintf(stderr, "  green by latitude (life is wettest and greenest at the equator)\n");
                fprintf(stderr, "    %5s %8s %8s %8s %8s\n", "lat", "green%", "rain", "moist",
                        "degC");
                for (int bnd = 0; bnd < 12; bnd++) {
                    if (zN[bnd] < 1) continue;
                    double lat = ((bnd + 0.5) / 12.0 - 0.5) * 180.0;
                    fprintf(stderr, "    %5.0f %7.1f%% %8.2f %8.2f %8.1f\n", lat,
                            100 * zG[bnd] / zN[bnd], zR[bnd] / zN[bnd], zM[bnd] / zN[bnd],
                            zT[bnd] / zN[bnd]);
                }
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
        fprintf(stderr, "\n%6s %7s %7s %8s %8s %7s %7s\n", "lat", "Tbl", "Tfree", "hP",
                "hWant", "u", "v");
        const double fbl = atmosphere::RHO * atmosphere::CP_AIR * atmosphere::H_LAYER /
                           atmosphere::C_AIR;
        for (int y = AH - 2; y >= 1; y -= 4) {
            double la = ((y + 0.5) / (double)AH - 0.5) * 180.0;
            double tb = 0, tf = 0, hp = 0, uu = 0, vv = 0;
            for (int x = 0; x < AW; x++) {
                int j = 2 * AW * AH + y * AW + x;
                tb += c.airT[j];
                tf += c.airTf[j];
                hp += c.press[j];
                uu += c.windU[j];
                vv += c.windV[j];
            }
            tb /= AW; tf /= AW; hp /= AW; uu /= AW; vv /= AW;
            double tcol = fbl * tb + (1.0 - fbl) * tf;
            fprintf(stderr, "%6.0f %7.1f %7.1f %8.1f %8.1f %7.1f %7.1f\n", la, tb, tf, hp,
                    -atmosphere::THERM_H_PER_K * (tcol + 25.0), uu, vv);
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
