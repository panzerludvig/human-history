// The heat need and the labour ledger, measured (Design/Resources.md).
// Builds one world's terrain, hydrology and climate, then runs the
// population twice from the same start -- hearths cold, hearths burning --
// and reports what the second need changed: who spends labour on wood,
// where the cold kills, and whether the map itself moved.
//
//   build_testresources.bat, then build\test_resources.exe [seed] [years]
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
#include "terrain.h"
#include "hydrology.h"
#include "atmosphere.h"
#include "sim.h"

struct Report {
    int settlements = 0;
    double people = 0;
    double coldYr = 0, starvedYr = 0;   // trailing-year losses, summed over settlements
    double coldSampled = 0;             // yearly samples of coldYr summed (approx. total)
    int coldTouched = 0;                // settlements losing anyone to cold this year
    int lowPile = 0;                    // under 30 days of firewood at this season
    // By wood cover: the treeless (steppe, tundra), the sparse, the wooded.
    double pBare = 0, pSparse = 0, pWooded = 0;
    int nBare = 0, nSparse = 0, nWooded = 0;
    double cutBare = 0, cutSparse = 0, cutWooded = 0; // labour share on wood (pop-weighted)
    double herdBare = 0;                              // herd per person on treeless ground
    // Farming and its reach:
    int farmers = 0;          // settlements practising farming
    double farmsteads = 0;    // standing farmsteads, world total
    int building = 0;         // farmsteads going up
    double workedShare = 0;   // farmEff / sFarm, farmer-population-weighted
    double farmerPop = 0;
};

static Report survey(const population::Field& pf, double now) {
    Report r;
    for (const population::Settlement& s : pf.settlements) {
        if (s.leaving || s.P <= 0) continue;
        r.settlements++;
        r.people += s.P;
        r.coldYr += s.coldYr;
        r.starvedYr += s.starvedYr;
        if (s.coldYr >= 0.5f) r.coldTouched++;
        float need = population::fuelNeedKg(population::cachedSeasonT(s, now)) *
                     std::max(s.P, 1.0f);
        if (s.fuelS / std::max(need, 1.0f) < 30.0f) r.lowPile++;
        if (s.sWood < 0.15f) {
            r.nBare++; r.pBare += s.P; r.cutBare += s.labFuel * s.P;
            r.herdBare += s.herd;
        } else if (s.sWood < 0.5f) {
            r.nSparse++; r.pSparse += s.P; r.cutSparse += s.labFuel * s.P;
        } else {
            r.nWooded++; r.pWooded += s.P; r.cutWooded += s.labFuel * s.P;
        }
        if (s.tech[population::TECH_FARMING].practising) {
            r.farmers++;
            r.farmsteads += s.farmsteads;
            if (s.fsteadWork > 0) r.building++;
            if (s.sFarm > 0.01f) {
                r.workedShare += s.farmEff / s.sFarm * s.P;
                r.farmerPop += s.P;
            }
        }
    }
    return r;
}

int main(int argc, char** argv) {
    uint32_t seed = argc >= 2 ? (uint32_t)strtoul(argv[1], nullptr, 10) : 7;
    int years = argc >= 3 ? atoi(argv[2]) : 500;
    float landPct = 30.0f, conc = 50.0f;

    // Same derivation as World::build (main.cpp).
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

    fprintf(stderr, "terrain and climate once...\n");
    plates::Field plf = plates::build(seed);
    float seaLevel = terrain::seaLevelFor(landPct / 100.0f, cp, rot, offset, plf);
    hydrology::Result hy = hydrology::build(cp, seaLevel, rot, offset, 12000.0f, plf);
    atmosphere::Climatology clim = atmosphere::build(cp, seaLevel, rot, offset, plf, hy, false);
    {
        std::vector<float> annual(atmosphere::W * atmosphere::H, 0.0f);
        std::vector<float> annualT(atmosphere::W * atmosphere::H, 0.0f);
        for (int i = 0; i < atmosphere::W * atmosphere::H; i++)
            for (int se = 0; se < atmosphere::SEASONS; se++) {
                annual[i] += clim.rainMmDay[se * atmosphere::W * atmosphere::H + i] /
                             atmosphere::SEASONS;
                annualT[i] += clim.meanT[se * atmosphere::W * atmosphere::H + i] /
                              atmosphere::SEASONS;
            }
        hydrology::reweight(hy, annual, annualT, atmosphere::W, atmosphere::H, 12000.0f);
    }

    Report out[2];
    for (int pass = 0; pass < 2; pass++) {
        population::HEAT_ENABLED = pass == 1;
        fprintf(stderr, "\n=== pass %d: hearths %s ===\n", pass,
                population::HEAT_ENABLED ? "burning" : "cold");
        population::Field pf = population::build(cp, seaLevel, rot, offset, plf, hy, &clim);
        technology::WorldState ws;
        technology::init(pf, ws, seed, 0.0);
        Report r;
        for (int y = 1; y <= years; y++) {
            sim::simulate(pf, ws, hy, clim, y * 365.0);
            if (population::HEAT_ENABLED) {
                for (const population::Settlement& s : pf.settlements)
                    if (!s.leaving) r.coldSampled += s.coldYr;
            }
            if (y % 100 == 0) {
                Report m = survey(pf, y * 365.0);
                fprintf(stderr,
                        "year %4d: %5d settlements, %8.0f people, cold/yr %6.0f, "
                        "hunger/yr %6.0f, low piles %4d\n",
                        y, m.settlements, m.people, m.coldYr, m.starvedYr, m.lowPile);
            }
        }
        Report m = survey(pf, years * 365.0);
        m.coldSampled = r.coldSampled;
        out[pass] = m;
    }

    const Report& c0 = out[0]; // hearths cold (baseline)
    const Report& c1 = out[1]; // hearths burning
    fprintf(stderr, "\n=== after %d years (baseline vs heat) ===\n", years);
    fprintf(stderr, "settlements       %6d      %6d\n", c0.settlements, c1.settlements);
    fprintf(stderr, "people            %8.0f    %8.0f\n", c0.people, c1.people);
    fprintf(stderr, "cold deaths/yr (trailing)     %8.0f\n", c1.coldYr);
    fprintf(stderr, "cold deaths, yearly samples summed over the run: %8.0f\n", c1.coldSampled);
    fprintf(stderr, "settlements touched by cold this year: %d; piles under 30 days: %d\n",
            c1.coldTouched, c1.lowPile);
    fprintf(stderr, "\nby wood cover (heat run): settlements, people, labour on wood\n");
    fprintf(stderr, "  treeless (<15%%)  %5d  %8.0f  %4.1f%%   herd/person %.2f\n", c1.nBare,
            c1.pBare, 100.0 * c1.cutBare / std::max(c1.pBare, 1.0),
            c1.herdBare / std::max(c1.pBare, 1.0));
    fprintf(stderr, "  sparse (15-50%%)  %5d  %8.0f  %4.1f%%\n", c1.nSparse, c1.pSparse,
            100.0 * c1.cutSparse / std::max(c1.pSparse, 1.0));
    fprintf(stderr, "  wooded (>50%%)    %5d  %8.0f  %4.1f%%\n", c1.nWooded, c1.pWooded,
            100.0 * c1.cutWooded / std::max(c1.pWooded, 1.0));
    fprintf(stderr, "\nbaseline by wood cover: treeless %d / %8.0f, sparse %d / %8.0f, wooded %d / %8.0f\n",
            c0.nBare, c0.pBare, c0.nSparse, c0.pSparse, c0.nWooded, c0.pWooded);
    for (int p = 0; p < 2; p++)
        fprintf(stderr,
                "%s: %d settlements farm; %.0f farmsteads stand, %d going up; "
                "worked land %.0f%% of the claim's potential (farmer-weighted)\n",
                p ? "heat    " : "baseline", out[p].farmers, out[p].farmsteads, out[p].building,
                100.0 * out[p].workedShare / std::max(out[p].farmerPop, 1.0));
    return 0;
}
