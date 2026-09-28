// The save file: a world's seed and menu parameters, the camera, and the
// whole standing population -- settlements, bands, cultures, land memory,
// ruins, game pools and the technology clock -- as plain text, one keyed
// line per thing, so a missing key takes its default and a new field is a
// new key rather than a shifted column. Technical/Globe Viewer.md §Menus and
// worlds describes the format's place in the game; every "version >= N"
// branch in load is one save format that is still readable.
//
// Everything derived from the seed is rebuilt on load, not stored: the file
// holds what the seed cannot regenerate.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include "camera.h"
#include "world.h"
#include "sim.h"

namespace savefile {

// Where the saves live: worlds\ beside the executable.
inline std::string worldsDir() { return world::exeDir() + "\\worlds"; }

// Write w and the camera to worlds\<w.name>.ibw. False if the file could
// not be opened or written.
inline bool save(const world::World& w, const camera::Camera& c) {
    CreateDirectoryA(worldsDir().c_str(), nullptr);
    std::ofstream f(worldsDir() + "\\" + w.name + ".ibw");
    if (!f) return false;
    f.precision(17);
    f << "version 24\n";
    f << "seed " << w.seed << "\n";
    f << "earth " << (w.earth ? 1 : 0) << "\n";
    f << "time " << w.simTime << "\n";
    f << "land " << w.landPercent << "\n";
    f << "concentration " << w.concentration << "\n";
    f << "lat " << c.lat << "\n";
    f << "lon " << c.lon << "\n";
    f << "altitude " << c.altitude << "\n";
    for (const population::Settlement& s : w.pop.settlements) {
        f << "settlement " << s.cell << " " << s.P << " " << s.R << " " << s.pop.C << " " << s.pop.M
          << " " << s.pop.W << " " << s.pop.E << " ";
        for (int t = 0; t < population::NTECH; t++)
            f << (int)s.tech[t].aware << " " << (int)s.tech[t].practising << " "
              << s.tech[t].practiceT << " " << s.tech[t].lostT << " ";
        f << s.S << " " << s.scarceSince << " " << s.founded << " " << s.herd << " " << s.granaries
          << " " << s.buildWork << " " << s.fillLo << " " << s.fillHi << " " << s.cycleT << " "
          << s.hungrySince << " " << s.granNeedYrs << " " << s.id << " " << s.starvedYr << " "
          << s.bows << " " << (s.name[0] ? s.name : "-") << " " << s.culture << " " << s.aff.hunt
          << " " << s.aff.gather << " " << s.aff.farm << " " << s.aff.herd << " " << s.aff.fight
          << " " << s.claimT;
        for (int k = 0; k < population::CLAIM_SECTORS; k++) f << " " << s.claim[k];
        f << " " << s.fuelS << " " << s.coldYr;
        f << " " << s.farmsteads << " " << s.fsteadWork;
        f << " " << s.tillWork << " " << (int)s.tillSite;
        for (int k = 0; k <= population::FSTEAD_MAX; k++) f << " " << s.tilled[k];
        f << "\n";
    }
    for (const population::Band& b : w.pop.bands) {
        f << "band " << b.id << " " << b.pop.C << " " << b.pop.M << " " << b.pop.W << " " << b.pop.E
          << " " << b.px << " " << b.py << " " << b.pz << " " << b.P << " " << b.S << " "
          << b.targetCell << " " << (int)b.resting << " " << b.restStart << " " << b.water;
        for (int t = 0; t < population::NTECH; t++)
            f << " " << (int)b.tech[t].aware << " " << (int)b.tech[t].practising << " "
              << b.tech[t].practiceT << " " << b.tech[t].lostT;
        f << " " << b.bows << " " << b.purpose << " " << b.homeId << " " << b.targetId << " "
          << (int)b.returning << " " << b.loot << " " << b.lootHerd << " " << b.sid << "\n";
    }
    f << "nextsid " << w.pop.nextSettlementId << "\n";
    for (const population::Culture& c : w.pop.cultures) {
        f << "culture " << c.name;
        for (int i = 0; i < 5; i++) f << " " << (int)c.onset[i];
        for (int i = 0; i < 4; i++) f << " " << (int)c.nucleus[i];
        for (int i = 0; i < 3; i++) f << " " << (int)c.coda[i];
        for (int i = 0; i < 2; i++) f << " " << (int)c.ending[i];
        f << "\n";
    }
    // Land memory and ruins: where people have lived and left.
    for (const auto& kv : w.pop.scars)
        f << "scar " << kv.first << " " << kv.second.R << " " << kv.second.t << "\n";
    for (const population::Field::Ruin& r : w.pop.ruins)
        f << "ruin " << r.cell << " " << r.abandoned << "\n";
    // Regional game pools: only the dented ones (the rest reload as 1).
    for (size_t r = 0; r < w.pop.gameG.size(); r++)
        if (w.pop.gameG[r] < 0.9999f) f << "game " << r << " " << w.pop.gameG[r] << "\n";
    f << "techrng " << w.tech.rng << "\n";
    return (bool)f;
}

// Read worlds\<name>.ibw into w and c: the world is rebuilt from its seed
// (reporting through ctx, and stopping when it is cancelled) and the saved
// population is restored on top of the regenerated field. Only the camera's
// position (lat, lon, altitude) is read into c. False if there is no such
// file or the rebuild was cancelled.
inline bool load(const std::string& name, world::World& w, camera::Camera& c,
                 const progress::Context& ctx) {
    std::ifstream f(worldsDir() + "\\" + name + ".ibw");
    if (!f) return false;
    w = world::World{};
    w.name = name;
    // Named fields, so adding a technology cannot silently shift a column.
    struct SavedSettlement {
        int cell = 0;
        double P = 0, R = 0, S = 0, scarce = -1, founded = 0, herd = 0;
        double granaries = 0, buildWork = 0, fillLo = 2, fillHi = -1, cycleT = 0;
        double hungrySince = -1, granNeed = 0, id = 0, starved = 0, bows = 0;
        double children = 0, men = 0, women = 0, elderly = 0;
        std::string name;
        double culture = 0, affHunt = 0, affGather = 0, affFarm = 0, affHerd = 0, affFight = 0;
        double claimT = 0, fuelS = -1, coldYr = 0, farmsteads = 0, fsteadWork = 0;
        double tillWork = 0, tillSite = -1;
        double tilled[1 + population::FSTEAD_MAX] = {};
        double claim[population::CLAIM_SECTORS] = {};
        double tech[population::NTECH][4] = {};
    };
    struct SavedBand {
        double id = 0, px = 0, py = 0, pz = 0, P = 0, S = 0;
        double target = -1, resting = 0, restStart = 0, bows = 0, water = -1;
        double purpose = 0, homeId = 0, targetId = 0, returning = 0, loot = 0, lootHerd = 0,
               sid = 0;
        double children = 0, men = 0, women = 0, elderly = 0;
        double tech[population::NTECH][4] = {};
    };
    std::vector<SavedSettlement> savedSettlements;
    std::vector<SavedBand> savedBands;
    std::vector<std::pair<size_t, double>> savedGame;
    std::vector<population::Culture> savedCultures;
    std::vector<std::array<double, 3>> savedScars;
    std::vector<std::pair<int, double>> savedRuins;
    double savedTime = 0;
    int version = 1;
    uint64_t savedTechRng = 0;
    std::string key;
    while (f >> key) {
        if (key == "version")
            f >> version;
        else if (key == "seed")
            f >> w.seed;
        else if (key == "earth") {
            int e = 0;
            f >> e;
            w.earth = e != 0;
        } else if (key == "time")
            f >> savedTime;
        else if (key == "techrng")
            f >> savedTechRng;
        else if (key == "settlement") {
            // cell P R, then [aware practising practiceT] per technology as
            // that version knew them, then the scalars each version added.
            SavedSettlement sv{};
            f >> sv.cell >> sv.P >> sv.R;
            if (version >= 15) f >> sv.children >> sv.men >> sv.women >> sv.elderly;
            int nt = version >= 19   ? 5
                     : version >= 13 ? 4
                     : version >= 8  ? 3
                     : version >= 3  ? 1
                                     : 0;
            for (int t = 0; t < nt; t++) {
                f >> sv.tech[t][0] >> sv.tech[t][1] >> sv.tech[t][2];
                sv.tech[t][3] = -1;
                if (version >= 20) f >> sv.tech[t][3];
            }
            if (version >= 7) {
                f >> sv.S >> sv.scarce >> sv.founded >> sv.herd;
                if (version >= 8)
                    f >> sv.granaries >> sv.buildWork >> sv.fillLo >> sv.fillHi >> sv.cycleT;
                if (version >= 9) f >> sv.hungrySince >> sv.granNeed;
                if (version >= 11) f >> sv.id;
                if (version >= 12) f >> sv.starved;
                if (version >= 13) f >> sv.bows;
                if (version >= 16)
                    f >> sv.name >> sv.culture >> sv.affHunt >> sv.affGather >> sv.affFarm >>
                        sv.affHerd >> sv.affFight;
                if (version >= 18) {
                    f >> sv.claimT;
                    for (int k = 0; k < population::CLAIM_SECTORS; k++) f >> sv.claim[k];
                }
                if (version >= 22) f >> sv.fuelS >> sv.coldYr;
                if (version >= 23) f >> sv.farmsteads >> sv.fsteadWork;
                if (version >= 24) {
                    f >> sv.tillWork >> sv.tillSite;
                    for (int k = 0; k <= population::FSTEAD_MAX; k++) f >> sv.tilled[k];
                }
            } else {
                if (version >= 4)
                    f >> sv.S >> sv.scarce;
                else
                    sv.S = 0.5 * population::CAP_DAYS_SETTLED * sv.P;
                if (version >= 6) f >> sv.founded;
            }
            savedSettlements.push_back(sv);
        } else if (key == "band") {
            SavedBand bv{};
            if (version >= 5) f >> bv.id;
            if (version >= 15) f >> bv.children >> bv.men >> bv.women >> bv.elderly;
            f >> bv.px >> bv.py >> bv.pz >> bv.P >> bv.S >> bv.target >> bv.resting >> bv.restStart;
            if (version >= 21) f >> bv.water;
            int nt = version >= 19   ? 5
                     : version >= 13 ? 4
                     : version >= 8  ? 3
                     : version >= 7  ? 2
                                     : 0;
            for (int t = 0; t < nt; t++) {
                f >> bv.tech[t][0] >> bv.tech[t][1] >> bv.tech[t][2];
                bv.tech[t][3] = -1;
                if (version >= 20) f >> bv.tech[t][3];
            }
            if (version >= 13) f >> bv.bows;
            if (version >= 14)
                f >> bv.purpose >> bv.homeId >> bv.targetId >> bv.returning >> bv.loot >>
                    bv.lootHerd;
            if (version >= 17) f >> bv.sid;
            savedBands.push_back(bv);
        } else if (key == "nextsid")
            f >> w.pop.nextSettlementId;
        else if (key == "culture") {
            population::Culture c{};
            std::string nm;
            f >> nm;
            for (int i = 0; i < 15 && i < (int)nm.size(); i++) c.name[i] = nm[i];
            int v;
            for (int i = 0; i < 5; i++) {
                f >> v;
                c.onset[i] = (uint8_t)v;
            }
            for (int i = 0; i < 4; i++) {
                f >> v;
                c.nucleus[i] = (uint8_t)v;
            }
            for (int i = 0; i < 3; i++) {
                f >> v;
                c.coda[i] = (uint8_t)v;
            }
            for (int i = 0; i < 2; i++) {
                f >> v;
                c.ending[i] = (uint8_t)v;
            }
            savedCultures.push_back(c);
        } else if (key == "scar") {
            double cell, R, t;
            f >> cell >> R >> t;
            savedScars.push_back({cell, R, t});
        } else if (key == "ruin") {
            int cell;
            double t;
            f >> cell >> t;
            savedRuins.push_back({cell, t});
        } else if (key == "game") {
            size_t r;
            double g;
            f >> r >> g;
            savedGame.push_back({r, g});
        } else if (key == "land")
            f >> w.landPercent;
        else if (key == "concentration")
            f >> w.concentration;
        else if (key == "lat")
            f >> c.lat;
        else if (key == "lon")
            f >> c.lon;
        else if (key == "altitude")
            f >> c.altitude;
        else {
            std::string skip;
            f >> skip;
        }
    }
    if (!w.build(ctx)) return false;
    // Restore the saved population on top of the regenerated field; local
    // properties come from the per-cell maps, so founded settlements restore
    // the same way as original ones.
    if (!savedSettlements.empty()) {
        w.pop.settlements.clear();
        w.pop.bands.clear();
        std::fill(w.pop.settlementAt.begin(), w.pop.settlementAt.end(), -1);
        for (auto& sv : savedSettlements) {
            int cell = sv.cell;
            if (cell < 0 || cell >= population::W * population::H) continue;
            population::Settlement st{cell,        0,           false,     {},
                                      (float)sv.P, (float)sv.R, savedTime, savedTime};
            // Older saves are headcounts only: give them the equilibrium
            // structure and let the flows take it from there.
            st.pop = sv.men + sv.women + sv.children + sv.elderly > 0.5
                         ? population::Cohorts{(float)sv.children, (float)sv.men, (float)sv.women,
                                               (float)sv.elderly}
                         : population::seedCohorts((float)sv.P);
            st.P = st.pop.total();
            st.kFoodP = w.pop.kFoodPMap[cell];
            st.kWater = w.pop.kWaterMap[cell];
            st.sFarm = w.pop.sFarmMap[cell];
            st.pasture = w.pop.pastureMap[cell];
            st.buildMat = w.pop.buildMatMap[cell];
            st.kGame = w.pop.kGameMap[cell];
            st.kSmall = w.pop.kSmallMap[cell];
            st.kFish = w.pop.kFishMap[cell];
            st.sFish = w.pop.sFishMap[cell];
            st.sWood = w.pop.sWoodMap[cell];
            // Saves that predate the hearth open with half a pile, as a new
            // world does -- not empty, or every old save thaws into a freeze.
            st.fuelS =
                sv.fuelS >= 0 ? (float)sv.fuelS : 0.5f * population::FUEL_CAP_KG * (float)sv.P;
            st.coldYr = (float)sv.coldYr;
            st.farmsteads = (float)sv.farmsteads;
            st.fsteadWork = (float)sv.fsteadWork;
            st.tillWork = (float)sv.tillWork;
            st.tillSite = (int8_t)sv.tillSite;
            for (int k = 0; k <= population::FSTEAD_MAX; k++) st.tilled[k] = (float)sv.tilled[k];
            // Saves that predate built plots hold farming villages with no
            // fields on record: back-fill what their hands would have
            // cleared by now, or every old farm starves on load.
            if (version < 24 && sv.tech[population::TECH_FARMING][1] > 0.5) {
                st.tilled[0] = std::min((float)sv.P * population::FARM_KM2_PER_PERSON,
                                        population::VILLAGE_FIELDS_KM2);
                for (int k = 0; k < (int)(st.farmsteads + 0.5f) && k < population::FSTEAD_MAX; k++)
                    st.tilled[k + 1] = population::FSTEAD_KM2;
            }
            st.gRegion = population::gameRegion(cell);
            for (int t = 0; t < population::NTECH; t++) {
                st.tech[t].aware = sv.tech[t][0] > 0.5;
                st.tech[t].practising = sv.tech[t][1] > 0.5;
                st.tech[t].practiceT = sv.tech[t][2];
                st.tech[t].lostT = sv.tech[t][3];
            }
            // Older saves predate archery as a technology: everyone knows it.
            if (!st.tech[population::TECH_ARCHERY].practising)
                st.tech[population::TECH_ARCHERY] = {true, true, savedTime};
            st.S = (float)sv.S;
            st.scarceSince = sv.scarce;
            st.founded = sv.founded;
            st.herd = (float)sv.herd;
            st.granaries = (float)sv.granaries;
            st.buildWork = (float)sv.buildWork;
            st.fillLo = (float)sv.fillLo;
            st.fillHi = (float)sv.fillHi;
            st.cycleT = version >= 8 ? sv.cycleT : savedTime;
            st.hungrySince = sv.hungrySince;
            st.granNeedYrs = (float)sv.granNeed;
            st.starvedYr = (float)sv.starved;
            st.bows = (float)sv.bows;
            st.culture = (uint16_t)sv.culture;
            st.aff = {(float)sv.affHunt, (float)sv.affGather, (float)sv.affFarm, (float)sv.affHerd,
                      (float)sv.affFight};
            if (sv.name.size() && sv.name != "-")
                for (int i = 0; i < 15 && i < (int)sv.name.size(); i++) st.name[i] = sv.name[i];
            // A claim from the save, or the floor for a world that predates
            // borders; the yields are rescaled to it below.
            st.claimT = sv.claimT;
            for (int k = 0; k < population::CLAIM_SECTORS; k++)
                st.claim[k] = sv.claim[k] > 0 ? (float)sv.claim[k] : population::CLAIM_FLOOR_KM;
            st.id = sv.id > 0 ? (uint32_t)sv.id : w.pop.nextSettlementId++;
            w.pop.nextSettlementId = std::max(w.pop.nextSettlementId, st.id + 1);
            if (st.tech[population::TECH_HUSBANDRY].practising && st.herd <= 0)
                st.herd = technology::HERD_SEED;
            atmosphere::seasonProfile(w.clim, sim::cellCentre(cell),
                                      std::max(w.hydro.heightM[cell], 0.0f), st.tSeason, st.meanF,
                                      st.meanG2);
            w.pop.settlementAt[cell] = (int)w.pop.settlements.size();
            w.pop.settlements.push_back(st);
        }
        for (auto& bv : savedBands) {
            population::Band b{};
            b.id = (uint32_t)bv.id;
            if (!b.id) b.id = w.pop.nextBandId;
            w.pop.nextBandId = std::max(w.pop.nextBandId, b.id + 1);
            b.px = (float)bv.px;
            b.py = (float)bv.py;
            b.pz = (float)bv.pz;
            b.P = (float)bv.P;
            b.S = (float)bv.S;
            b.pop = bv.children + bv.men + bv.women + bv.elderly > 0.5
                        ? population::Cohorts{(float)bv.children, (float)bv.men, (float)bv.women,
                                              (float)bv.elderly}
                        : population::seedCohorts(b.P);
            b.P = b.pop.total();
            b.targetCell = (int)bv.target;
            b.resting = bv.resting > 0.5;
            b.restStart = bv.restStart;
            b.bows = (float)bv.bows;
            b.water = bv.water >= 0 ? (float)bv.water : population::CAP_WATER_DAYS * (float)bv.P;
            b.purpose = (int)bv.purpose;
            b.homeId = (uint32_t)bv.homeId;
            b.targetId = (uint32_t)bv.targetId;
            b.sid = (uint32_t)bv.sid;
            b.returning = bv.returning > 0.5;
            b.loot = (float)bv.loot;
            b.lootHerd = (float)bv.lootHerd;
            for (int t = 0; t < population::NTECH; t++) {
                b.tech[t].aware = bv.tech[t][0] > 0.5;
                b.tech[t].practising = bv.tech[t][1] > 0.5;
                b.tech[t].practiceT = bv.tech[t][2];
                b.tech[t].lostT = bv.tech[t][3];
            }
            if (!b.tech[population::TECH_ARCHERY].practising)
                b.tech[population::TECH_ARCHERY] = {true, true, savedTime};
            b.t = savedTime;
            b.nextUpdate = savedTime;
            if (b.targetCell >= 0 && b.targetCell < population::W * population::H)
                w.pop.bands.push_back(b);
        }
        population::computeNeighbours(w.pop);
    }
    if (!savedCultures.empty()) w.pop.cultures = savedCultures;
    // The roll of names is not saved; it is exactly what is standing and
    // walking, so rebuild it rather than store it.
    w.pop.takenNames.clear();
    for (const population::Culture& cu : w.pop.cultures) w.pop.takenNames.insert(cu.name);
    for (const population::Settlement& st : w.pop.settlements) w.pop.takenNames.insert(st.name);
    for (const population::Band& bd : w.pop.bands) w.pop.takenNames.insert(bd.name);
    for (auto& sc : savedScars)
        if (sc[0] >= 0 && sc[0] < population::W * population::H)
            w.pop.scars[(int)sc[0]] = {(float)sc[1], sc[2]};
    for (auto& rn : savedRuins)
        if (rn.first >= 0 && rn.first < population::W * population::H)
            w.pop.ruins.push_back({rn.first, rn.second});
    // Restore the game pools (default pristine), then refresh each
    // settlement's cached health.
    for (auto& [r, g] : savedGame)
        if (r < w.pop.gameG.size()) w.pop.gameG[r] = (float)g;
    w.pop.gameT = savedTime;
    for (population::Settlement& st : w.pop.settlements)
        if (st.kGame > 0) st.gameNow = w.pop.gameG[st.gRegion];
    // Cache what the standing fields feed before anything asks (panels read
    // farmK before the first simulate step).
    for (population::Settlement& st : w.pop.settlements) sim::updateFarmland(w.pop, st);
    w.simTime = savedTime;
    if (savedTechRng) w.tech.rng = savedTechRng;
    // Contact draws and the invention clock are exponential (memoryless), so
    // redrawing them on load is statistically exact.
    for (int t = 0; t < population::NTECH; t++) {
        for (int i = 0; i < (int)w.pop.settlements.size(); i++)
            technology::redraw(w.pop, i, w.tech, t, savedTime);
        technology::scheduleInvention(w.pop, w.tech, t, savedTime);
    }
    c.clampAltitude();
    return true;
}

// The names of every save, sorted.
inline std::vector<std::string> list() {
    std::vector<std::string> names;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((worldsDir() + "\\*.ibw").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return names;
    do {
        std::string n = fd.cFileName;
        names.push_back(n.substr(0, n.size() - 4));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace savefile
