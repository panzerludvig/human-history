// The heat need and the labour ledger, measured (Design/Resources.md).
// Builds one world's terrain, hydrology and climate, then runs the
// population twice from the same start -- hearths cold, hearths burning --
// and reports what the second need changed: who spends labour on wood,
// where the cold kills, and whether the map itself moved. Every sub-step of
// both runs is audited against the day's labour budget (LedgerAudit).
//
// Fields going back to the wild (work order 12) are measured too: the
// tilled land still held by settlements that no longer farm, by how long
// ago they lapsed, and the farming lapses for want of means among
// settlements that never had a field. With `lapse`, farming is ended
// everywhere at year 300 (and kept ended) and the world's tilled km2 is
// printed at years 300, 310, 325, 350 and 360 -- the reversion clock.
//
//   build_testresources.bat, then build\test_resources.exe [seed] [years] [lapse]
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>
#include "world.h"
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
    int clearing = 0;         // plots being cleared
    double tilledKm2 = 0;     // world total under the rotation
    double farmFed = 0;       // people the fields feed at current expertise
    double farmerPop = 0;
    double granaries = 0;           // standing granaries, world total
    population::LedgerAudit ledger; // the day's labour, audited every sub-step
    // Fields nobody farms: tilled km2 held by settlements not practising,
    // by whether they lapsed within FIELD_REVERT_YEARS or earlier.
    double idleRecentKm2 = 0, idleOldKm2 = 0;
    int idleOldSettlements = 0;
    double worldTilledKm2 = 0; // every settlement's, farming or not
    // Counted over the run, not surveyed:
    int farmLapses = 0;        // farming lapses
    int farmLapsesNoField = 0; // ...of those, for want of means, never having had a field
    int forcedReadoptions = 0; // `lapse` mode: farming found practised again after year 300
};

// Who farmed at the last yearly look, and when each settlement's farming
// last lapsed, by settlement id. TechState::lostT cannot say this: it is
// also moved forward while any neighbour still practises.
struct LapseLog {
    std::unordered_map<uint32_t, bool> farming;
    std::unordered_map<uint32_t, double> lapsedDay;
};

static double tilledOf(const population::Settlement& s) {
    double t = 0;
    for (int k = 0; k <= population::FSTEAD_MAX; k++) t += s.tilled[k];
    return t;
}

static Report survey(const population::Field& pf, double now, const LapseLog& log) {
    Report r;
    for (const population::Settlement& s : pf.settlements) {
        if (s.leaving || s.P <= 0) continue;
        r.settlements++;
        r.people += s.P;
        r.granaries += s.granaries;
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
        r.worldTilledKm2 += tilledOf(s);
        const population::TechState& farm = s.tech[population::TECH_FARMING];
        if (!farm.practising && tilledOf(s) > 0) {
            // A holder of land never seen to lapse counts as old: it should
            // not exist, and fails the check loudly.
            auto at = log.lapsedDay.find(s.id);
            if (at != log.lapsedDay.end() &&
                now - at->second <= population::FIELD_REVERT_YEARS * 365.0) {
                r.idleRecentKm2 += tilledOf(s);
            } else {
                r.idleOldKm2 += tilledOf(s);
                r.idleOldSettlements++;
            }
        }
        if (s.tech[population::TECH_FARMING].practising) {
            r.farmers++;
            r.farmsteads += s.farmsteads;
            if (s.fsteadWork > 0) r.building++;
            if (s.tillWork > 0) r.clearing++;
            r.tilledKm2 += tilledOf(s);
            r.farmFed += s.farmK * technology::expertise(
                                       s.tech[population::TECH_FARMING], now);
            r.farmerPop += s.P;
        }
    }
    return r;
}

// Farming lapses in the year ending at `now`: settlements that farmed at
// the last look and do not now. The means are judged as
// technology::decaySkills judged them: for a settlement that never had a
// field that is the ground, which does not change, so judging it at the
// year's end is judging it at the lapse.
static void countLapses(const population::Field& pf, double now, LapseLog& log, Report& r) {
    for (const population::Settlement& s : pf.settlements) {
        const population::TechState& ts = s.tech[population::TECH_FARMING];
        bool& was = log.farming[s.id];
        if (was && !ts.practising) {
            r.farmLapses++;
            log.lapsedDay[s.id] = ts.lostT >= 0 ? ts.lostT : now;
            if (!s.hadFields && !technology::meansPresent(s, population::TECH_FARMING))
                r.farmLapsesNoField++;
        }
        was = ts.practising;
    }
}

// `lapse` mode: nobody farms from year 300. Every settlement and band that
// practises farming stops, as technology::decaySkills stops it, and the
// neighbours are redrawn so nobody takes it up again from them.
static int endFarming(population::Field& pf, technology::WorldState& ws, double now) {
    int ended = 0;
    const int tech = population::TECH_FARMING;
    for (int i = 0; i < (int)pf.settlements.size(); i++) {
        population::TechState& ts = pf.settlements[i].tech[tech];
        if (!ts.practising) continue;
        ts.practising = false;
        ts.strainT = -1;
        ts.lostT = now;
        ended++;
    }
    for (population::Band& b : pf.bands)
        if (b.tech[tech].practising) {
            b.tech[tech].practising = false;
            b.tech[tech].lostT = now;
            ended++;
        }
    for (int i = 0; i < (int)pf.settlements.size(); i++) technology::redraw(pf, i, ws, tech, now);
    technology::scheduleInvention(pf, ws, tech, now);
    return ended;
}

int main(int argc, char** argv) {
    world::World globe;
    globe.seed = argc >= 2 ? (uint32_t)strtoul(argv[1], nullptr, 10) : 7;
    int years = argc >= 3 ? atoi(argv[2]) : 500;
    const bool forceLapse = argc >= 4 && strcmp(argv[3], "lapse") == 0;
    const int LAPSE_YEAR = 300;
    const int lapseProbeYears[] = {300, 310, 325, 350, 360};
    double lapseTilledKm2[2][5] = {};
    globe.concentration = 50.0f; // land 30%, the default

    fprintf(stderr, "terrain and climate once...\n");
    globe.build(nullptr, world::Stage::Rivers);
    const uint32_t seed = globe.seed;
    const float* rot = globe.rot;
    const terrain::V3 offset = globe.terrainOffset();
    const terrain::ContinentParams& cp = globe.cp;
    const plates::Field& plf = globe.plateField;
    hydrology::Result& hy = globe.hydro;
    atmosphere::Climatology& clim = globe.clim;
    const float seaLevel = globe.seaLevel;

    Report out[2];
    for (int pass = 0; pass < 2; pass++) {
        population::HEAT_ENABLED = pass == 1;
        fprintf(stderr, "\n=== pass %d: hearths %s ===\n", pass,
                population::HEAT_ENABLED ? "burning" : "cold");
        population::Field pf = population::build(cp, seaLevel, rot, offset, plf, hy, &clim);
        technology::WorldState ws;
        technology::init(pf, ws, seed, 0.0);
        Report r;
        population::LEDGER_AUDIT = &r.ledger;
        LapseLog log;
        for (int y = 1; y <= years; y++) {
            sim::simulate(pf, ws, hy, clim, y * 365.0);
            countLapses(pf, y * 365.0, log, r);
            if (forceLapse) {
                for (int k = 0; k < 5; k++)
                    if (y == lapseProbeYears[k])
                        lapseTilledKm2[pass][k] = survey(pf, y * 365.0, log).worldTilledKm2;
                if (y == LAPSE_YEAR) endFarming(pf, ws, y * 365.0);
                if (y > LAPSE_YEAR) r.forcedReadoptions += endFarming(pf, ws, y * 365.0);
            }
            if (population::HEAT_ENABLED) {
                for (const population::Settlement& s : pf.settlements)
                    if (!s.leaving) r.coldSampled += s.coldYr;
            }
            if (y % 100 == 0) {
                Report m = survey(pf, y * 365.0, log);
                fprintf(stderr,
                        "year %4d: %5d settlements, %8.0f people, cold/yr %6.0f, "
                        "hunger/yr %6.0f, low piles %4d\n",
                        y, m.settlements, m.people, m.coldYr, m.starvedYr, m.lowPile);
            }
        }
        Report m = survey(pf, years * 365.0, log);
        m.coldSampled = r.coldSampled;
        m.ledger = r.ledger;
        population::LEDGER_AUDIT = nullptr;
        m.farmLapses = r.farmLapses;
        m.farmLapsesNoField = r.farmLapsesNoField;
        m.forcedReadoptions = r.forcedReadoptions;
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
                "%s: %d settlements farm (%.0f people); %.0f km2 tilled, %d plots being "
                "cleared; fields feed %.0f; %.0f farmsteads stand, %d going up\n",
                p ? "heat    " : "baseline", out[p].farmers, out[p].farmerPop,
                out[p].tilledKm2, out[p].clearing, out[p].farmFed, out[p].farmsteads,
                out[p].building);
    for (int p = 0; p < 2; p++)
        fprintf(stderr, "%s: %.0f granaries stand\n", p ? "heat    " : "baseline",
                out[p].granaries);
    // Every draw comes out of the one budget: the worst sub-step's
    // (food + heat + projects) / budget, the settlement-days (sub-steps)
    // spent over it, and those in which a project drew labour with the
    // stores at or below the hoarding threshold. Both counts must be zero.
    for (int p = 0; p < 2; p++) {
        const population::LedgerAudit& a = out[p].ledger;
        fprintf(stderr,
                "%s ledger: max labour/budget %.4f; over budget %.0f settlement-days "
                "(%lld sub-steps); projects in famine %.0f settlement-days (%lld sub-steps)\n",
                p ? "heat    " : "baseline", a.maxRatio, a.overDays, a.overSteps, a.famineDays,
                a.famineSteps);
    }
    fprintf(stderr, "\nfields nobody farms (work order 12):\n");
    for (int p = 0; p < 2; p++) {
        fprintf(stderr,
                "%s: farming lapses %d, of which for want of means never having had a field "
                "%d\n",
                p ? "heat    " : "baseline", out[p].farmLapses, out[p].farmLapsesNoField);
        fprintf(stderr,
                "%s: tilled km2 held by settlements not farming: lapsed within %.0f years "
                "%.1f, earlier %.1f (%d settlements)\n",
                p ? "heat    " : "baseline", population::FIELD_REVERT_YEARS, out[p].idleRecentKm2,
                out[p].idleOldKm2, out[p].idleOldSettlements);
    }
    if (forceLapse) {
        fprintf(stderr, "\nforced lapse at year %d: world tilled km2\n", LAPSE_YEAR);
        for (int p = 0; p < 2; p++) {
            fprintf(stderr, "%s:", p ? "heat    " : "baseline");
            for (int k = 0; k < 5; k++)
                if (lapseProbeYears[k] <= years)
                    fprintf(stderr, "  y%d %.1f", lapseProbeYears[k], lapseTilledKm2[p][k]);
            fprintf(stderr, "  (farming found again and re-ended: %d)\n", out[p].forcedReadoptions);
        }
    }
    return 0;
}
