// Round-trips a world through the save file (src/savefile.h): builds a
// world from a seed, runs the population for a few years so there is state
// worth keeping, saves it, loads it back into a fresh world and compares
// every field the file carries -- settlements, bands, cultures, land
// memory, ruins, game pools, the technology clock, the sim time and the
// camera. Fails loud on any mismatch. Before this probe the version
// branches in savefile::load had no test at all (work order 05).
//
//   build_testsavefile.bat, then build\test_savefile.exe [seed] [years]
//
// The save is written to worlds\roundtrip-probe.ibw beside the exe and
// deleted afterwards.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "camera.h"
#include "world.h"
#include "sim.h"
#include "savefile.h"

static int compared = 0;
static int failures = 0;

static void logStage(const char* stage) {
    if (stage[0]) fprintf(stderr, "build: %s\n", stage);
}

static void check(const char* what, int i, double saved, double loaded, double tol = 0.0) {
    compared++;
    if (std::fabs(saved - loaded) <= tol) return;
    failures++;
    if (failures <= 40)
        fprintf(stderr, "MISMATCH %s[%d]: saved %.17g, loaded %.17g\n", what, i, saved, loaded);
}

static void checkStr(const char* what, int i, const char* saved, const char* loaded) {
    compared++;
    if (strcmp(saved, loaded) == 0) return;
    failures++;
    if (failures <= 40)
        fprintf(stderr, "MISMATCH %s[%d]: saved \"%s\", loaded \"%s\"\n", what, i, saved, loaded);
}

static void checkTech(const char* what, int i, const population::TechState* a,
                      const population::TechState* b) {
    for (int t = 0; t < population::NTECH; t++) {
        check(what, i * 100 + t, a[t].aware, b[t].aware);
        check(what, i * 100 + t, a[t].practising, b[t].practising);
        check(what, i * 100 + t, a[t].practiceT, b[t].practiceT);
        check(what, i * 100 + t, a[t].lostT, b[t].lostT);
    }
}

static void compareSettlements(const population::Field& a, const population::Field& b) {
    check("settlements.size", 0, (double)a.settlements.size(), (double)b.settlements.size());
    for (size_t i = 0; i < a.settlements.size() && i < b.settlements.size(); i++) {
        const population::Settlement& s = a.settlements[i];
        const population::Settlement& l = b.settlements[i];
        int k = (int)i;
        check("cell", k, s.cell, l.cell);
        check("id", k, s.id, l.id);
        check("pop.C", k, s.pop.C, l.pop.C);
        check("pop.M", k, s.pop.M, l.pop.M);
        check("pop.W", k, s.pop.W, l.pop.W);
        check("pop.E", k, s.pop.E, l.pop.E);
        // P is re-summed from the cohorts on load: a float sum, so tolerance.
        check("P", k, s.P, l.P, 1e-3 * std::max(1.0f, s.P));
        check("R", k, s.R, l.R);
        check("S", k, s.S, l.S);
        check("scarceSince", k, s.scarceSince, l.scarceSince);
        check("founded", k, s.founded, l.founded);
        check("herd", k, s.herd, l.herd);
        check("granaries", k, s.granaries, l.granaries);
        check("buildWork", k, s.buildWork, l.buildWork);
        check("fillLo", k, s.fillLo, l.fillLo);
        check("fillHi", k, s.fillHi, l.fillHi);
        check("cycleT", k, s.cycleT, l.cycleT);
        check("hungrySince", k, s.hungrySince, l.hungrySince);
        check("granNeedYrs", k, s.granNeedYrs, l.granNeedYrs);
        check("starvedYr", k, s.starvedYr, l.starvedYr);
        check("bows", k, s.bows, l.bows);
        checkStr("name", k, s.name, l.name);
        check("culture", k, s.culture, l.culture);
        check("aff.hunt", k, s.aff.hunt, l.aff.hunt);
        check("aff.gather", k, s.aff.gather, l.aff.gather);
        check("aff.farm", k, s.aff.farm, l.aff.farm);
        check("aff.herd", k, s.aff.herd, l.aff.herd);
        check("aff.fight", k, s.aff.fight, l.aff.fight);
        check("claimT", k, s.claimT, l.claimT);
        for (int c = 0; c < population::CLAIM_SECTORS; c++)
            check("claim", k * 100 + c, s.claim[c], l.claim[c]);
        check("fuelS", k, s.fuelS, l.fuelS);
        check("coldYr", k, s.coldYr, l.coldYr);
        check("farmsteads", k, s.farmsteads, l.farmsteads);
        check("fsteadWork", k, s.fsteadWork, l.fsteadWork);
        check("tillWork", k, s.tillWork, l.tillWork);
        check("tillSite", k, s.tillSite, l.tillSite);
        for (int c = 0; c <= population::FSTEAD_MAX; c++)
            check("tilled", k * 100 + c, s.tilled[c], l.tilled[c]);
        checkTech("tech", k, s.tech, l.tech);
    }
}

static void compareBands(const population::Field& a, const population::Field& b) {
    check("bands.size", 0, (double)a.bands.size(), (double)b.bands.size());
    for (size_t i = 0; i < a.bands.size() && i < b.bands.size(); i++) {
        const population::Band& s = a.bands[i];
        const population::Band& l = b.bands[i];
        int k = (int)i;
        check("band.id", k, s.id, l.id);
        check("band.pop.C", k, s.pop.C, l.pop.C);
        check("band.pop.M", k, s.pop.M, l.pop.M);
        check("band.pop.W", k, s.pop.W, l.pop.W);
        check("band.pop.E", k, s.pop.E, l.pop.E);
        check("band.px", k, s.px, l.px);
        check("band.py", k, s.py, l.py);
        check("band.pz", k, s.pz, l.pz);
        check("band.P", k, s.P, l.P, 1e-3 * std::max(1.0f, s.P));
        check("band.S", k, s.S, l.S);
        check("band.targetCell", k, s.targetCell, l.targetCell);
        check("band.resting", k, s.resting, l.resting);
        check("band.restStart", k, s.restStart, l.restStart);
        check("band.water", k, s.water, l.water);
        check("band.bows", k, s.bows, l.bows);
        check("band.purpose", k, s.purpose, l.purpose);
        check("band.homeId", k, s.homeId, l.homeId);
        check("band.targetId", k, s.targetId, l.targetId);
        check("band.returning", k, s.returning, l.returning);
        check("band.loot", k, s.loot, l.loot);
        check("band.lootHerd", k, s.lootHerd, l.lootHerd);
        check("band.sid", k, s.sid, l.sid);
        checkTech("band.tech", k, s.tech, l.tech);
    }
}

static void compareRest(const population::Field& a, const population::Field& b) {
    check("nextSettlementId", 0, a.nextSettlementId, b.nextSettlementId);
    check("cultures.size", 0, (double)a.cultures.size(), (double)b.cultures.size());
    for (size_t i = 0; i < a.cultures.size() && i < b.cultures.size(); i++) {
        const population::Culture& s = a.cultures[i];
        const population::Culture& l = b.cultures[i];
        checkStr("culture.name", (int)i, s.name, l.name);
        for (int j = 0; j < 5; j++) check("culture.onset", (int)i * 10 + j, s.onset[j], l.onset[j]);
        for (int j = 0; j < 4; j++)
            check("culture.nucleus", (int)i * 10 + j, s.nucleus[j], l.nucleus[j]);
        for (int j = 0; j < 3; j++) check("culture.coda", (int)i * 10 + j, s.coda[j], l.coda[j]);
        for (int j = 0; j < 2; j++)
            check("culture.ending", (int)i * 10 + j, s.ending[j], l.ending[j]);
    }
    check("scars.size", 0, (double)a.scars.size(), (double)b.scars.size());
    for (const auto& kv : a.scars) {
        auto it = b.scars.find(kv.first);
        if (it == b.scars.end()) {
            failures++;
            fprintf(stderr, "MISMATCH scar at cell %d missing after load\n", kv.first);
            continue;
        }
        check("scar.R", kv.first, kv.second.R, it->second.R);
        check("scar.t", kv.first, kv.second.t, it->second.t);
    }
    check("ruins.size", 0, (double)a.ruins.size(), (double)b.ruins.size());
    for (size_t i = 0; i < a.ruins.size() && i < b.ruins.size(); i++) {
        check("ruin.cell", (int)i, a.ruins[i].cell, b.ruins[i].cell);
        check("ruin.abandoned", (int)i, a.ruins[i].abandoned, b.ruins[i].abandoned);
    }
    check("gameG.size", 0, (double)a.gameG.size(), (double)b.gameG.size());
    // Only dented pools are written; the rest reload as exactly 1.
    for (size_t i = 0; i < a.gameG.size() && i < b.gameG.size(); i++)
        check("gameG", (int)i, a.gameG[i] < 0.9999f ? a.gameG[i] : 1.0f, b.gameG[i]);
}

int main(int argc, char** argv) {
    uint32_t seed = argc >= 2 ? (uint32_t)strtoul(argv[1], nullptr, 10) : 7u;
    double years = argc >= 3 ? atof(argv[2]) : 3.0;

    world::World w;
    w.seed = seed;
    w.name = "roundtrip-probe";
    w.build(logStage);
    w.simTime += years * 365.0;
    sim::simulate(w.pop, w.tech, w.hydro, w.clim, w.simTime);
    camera::Camera c;
    c.lat = 0.5;
    c.lon = -1.2;
    c.altitude = 0.1;
    c.clampAltitude();
    fprintf(stderr,
            "saved: seed %u, year %.1f, %d settlements, %d bands, %d cultures, "
            "%d scars, %d ruins\n",
            seed, years, (int)w.pop.settlements.size(), (int)w.pop.bands.size(),
            (int)w.pop.cultures.size(), (int)w.pop.scars.size(), (int)w.pop.ruins.size());
    if (!savefile::save(w, c)) {
        fprintf(stderr, "FAIL: save returned false\n");
        return 1;
    }

    world::World l;
    camera::Camera lc;
    if (!savefile::load(w.name, l, lc, logStage)) {
        fprintf(stderr, "FAIL: load returned false\n");
        return 1;
    }
    std::string path = savefile::worldsDir() + "\\" + w.name + ".ibw";
    DeleteFileA(path.c_str());

    check("seed", 0, w.seed, l.seed);
    check("earth", 0, w.earth, l.earth);
    check("landPercent", 0, w.landPercent, l.landPercent);
    check("concentration", 0, w.concentration, l.concentration);
    check("simTime", 0, w.simTime, l.simTime);
    // The technology generator is restored and then advanced: load redraws
    // every contact and invention clock from it (memoryless, so exact in
    // distribution), so equality is not expected -- only that it was read.
    check("tech.rng nonzero", 0, 1.0, l.tech.rng != 0 ? 1.0 : 0.0);
    check("cam.lat", 0, c.lat, lc.lat);
    check("cam.lon", 0, c.lon, lc.lon);
    check("cam.altitude", 0, c.altitude, lc.altitude);
    compareSettlements(w.pop, l.pop);
    compareBands(w.pop, l.pop);
    compareRest(w.pop, l.pop);

    printf("roundtrip seed %u year %.1f: %d fields compared, %d mismatches -- %s\n", seed, years,
           compared, failures, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
