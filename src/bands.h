// Migration (Design/Migration.md): a band's journey from the decision to
// set out to the ground it settles. The choice to relocate or split, what
// a mover with these skills could make of a cell, the best prospect in
// reach, the march itself -- pace, water, hunger -- and the arrival: found
// a settlement, join one, or die on the road.
#pragma once
#include "events.h"
#include "claims.h"
#include "farmland.h"
#include "raids.h"
#include <cmath>
#include <cstdio>

namespace sim {

// Passability (Design/Migration.md): nothing blocks outright. Unfrozen open
// water is crossed at half speed (rafts); frozen water is walked at full
// speed (winter is the crossing season -- ice-bridge migrations); a major
// unfrozen river slows a band to fording pace for the cell it crosses.
// Ice forms between two seasonal local temperatures (degC): below
// ICE_FORMING_T it starts, and below FROZEN_T it is fully formed. Bands walk
// on water, and melt frozen sea to drink, only below FROZEN_T; between the two
// the ice is still forming and is crossed as open water. The globe draws the
// same ramp from these two constants (globe.frag iceAt, which receives them
// from main.cpp), so the sim walks only on ice the map shows as solid.
constexpr float ICE_FORMING_T = -1.0f;
constexpr float FROZEN_T = -4.0f;
constexpr float RAFT_FACTOR = 0.5f;
constexpr float RIVER_CROSS_FACTOR = 0.4f;
constexpr float RIVER_MAJOR_KM2 = 40000.0f; // runoff-equivalent area

// Prominence: how far the site rises above its regional (climate-grid) mean
// elevation. The vantage input to awareness.
inline float prominenceM(const hydrology::Result& hy, const atmosphere::Climatology& clim,
                         int cell) {
    if (clim.elev.empty()) return 0.0f;
    float h = std::max(hy.heightM[cell], 0.0f);
    int x = cell % population::W, y = cell / population::W;
    int ax = x * atmosphere::W / population::W, ay = y * atmosphere::H / population::H;
    return h - clim.elev[ay * atmosphere::W + ax];
}

// Season-interpolated local temperature: coarse climate mean, lapse-corrected
// from the model's smoothed elevation to the local height.
inline float seasonalT(const atmosphere::Climatology& c, terrain::V3 n, float hLocal, double now) {
    return atmosphere::seasonalTempC(c, n, hLocal, now);
}

// The capacity a mover with these skills would command at a cell: forager
// yield plus the farming and herding bonuses for what it practises. This is
// how a herding band values the steppe a forager walks past -- and it must
// gate founding too, or a band would choose a target it then refuses.
inline float moverCap(const population::Field& pf, int cell, float farmExp, float husbExp,
                      double now, float fishExp = 0, float movers = 300.0f) {
    float food = pf.kFoodPMap[cell];
    if (food <= 0) return 0;
    // What a farming mover could make of this ground: the fields their
    // hands could till here, at its suitability -- prospective, since the
    // plots would still have to be cleared on arrival.
    food += population::TILLED_YIELD_PKM2 * pf.sFarmMap[cell] * farmExp *
            std::min(movers * population::FARM_KM2_PER_PERSON, population::VILLAGE_FIELDS_KM2);
    if (husbExp > 0)
        food += pf.pastureMap[cell] * population::FORAGE_KM2 * population::HERD_PASTURE_K /
                population::SUSTAIN_R * (0.3f + 0.7f * husbExp) * 0.85f;
    // The water counts even to people with no gear -- anyone can take fish
    // from a bank -- and counts for much more to people who can weir it.
    food += pf.kFishMap[cell] * population::fishEff(fishExp);
    return std::min(food, pf.kWaterMap[cell]);
}

// The same, priced for land that has been lived on and left: an exhausted
// valley is a bad place to move to for a generation. Only applied to the
// finalists of a search -- scars are sparse, and the lookup is not free.
inline float moverCapScarred(const population::Field& pf, int cell, float farmExp, float husbExp,
                             double now, float fishExp = 0, float movers = 300.0f) {
    return moverCap(pf, cell, farmExp, husbExp, now, fishExp, movers) *
           population::cellCondition(pf, cell, now);
}

// The same, priced for the room there is to claim: hemmed-in ground is worth
// what its gap allows, open country what one people could ever hold. This is
// what sends colonists to the frontier ahead of the gaps behind them, and it
// is why claims have anywhere to grow.
inline float moverCapRoom(const population::Field& pf, int cell, float farmExp, float husbExp,
                          double now, float movers, float fishExp = 0) {
    float room = std::min(roomKm(pf, cellCentre(cell)), population::CLAIM_CAP_KM);
    if (room < population::CLAIM_FLOOR_KM) return 0.0f;
    // What they would hold on arrival: what they need, or what fits.
    float take = std::min(wantedReachKm(pf, cell, movers, population::SUSTAIN_R), room);
    return moverCapScarred(pf, cell, farmExp, husbExp, now, fishExp, std::max(movers, 1.0f)) *
           claimFactor(take);
}

// The best-looking unclaimed prospect within the knowledge range, judged with
// noise that grows with distance: near things resolve exactly, far things are
// rumours; each candidate is valued at what THIS mover could make of it.
// Returns a cell index, or -1 if nothing known is worth going to.
inline int bestProspect(const population::Field& pf, terrain::V3 from, uint64_t& rng,
                        float radiusKm, double now, float farmExp = 0, float husbExp = 0,
                        float* estOut = nullptr, float movers = 0, float fishExp = 0) {
    bool skilled = farmExp > 0 || husbExp > 0 || fishExp > 0;
    float lat0 = std::asin(std::clamp(from.z, -1.0f, 1.0f));
    float dLat = radiusKm / 6371.0f;
    int y0 = std::max((int)(((lat0 - dLat) + 3.14159265f / 2) / 3.14159265f * population::H), 1);
    int y1 = std::min((int)(((lat0 + dLat) + 3.14159265f / 2) / 3.14159265f * population::H) + 1,
                      population::H - 2);
    // Cheap scoring pass (chord distance, no spacing checks), then the
    // expensive spacing check only on the best few in score order.
    struct Cand {
        float est;
        int cell;
    };
    Cand top[24];
    int nTop = 0;
    for (int y = y0; y <= y1; y += 2)
        for (int x = 0; x < population::W; x += 2) {
            int cell = y * population::W + x;
            // Cheap exact rejections first -- this scan runs over thousands
            // of cells per search and every search is a settlement deciding
            // its future. Water caps any mover's capacity, and a mover with
            // no skills is worth exactly K.
            if (pf.kWaterMap[cell] < population::MIN_SETTLEMENT_K) continue;
            if (!skilled && pf.K[cell] < population::MIN_SETTLEMENT_K) continue;
            float cap = skilled ? moverCap(pf, cell, farmExp, husbExp, now, fishExp,
                                           movers > 0 ? movers : 300.0f)
                                : pf.K[cell];
            if (cap < population::MIN_SETTLEMENT_K) continue;
            terrain::V3 n = cellCentre(cell);
            float dot = terrain::dot(from, n);
            float d = 6371.0f * std::sqrt(std::max(2.0f - 2.0f * dot, 0.0f)); // chord ~ arc
            if (d > radiusKm || d < 80.0f) continue;
            float noise = ((float)technology::urand(rng) * 2.0f - 1.0f) * 0.6f * (d / radiusKm);
            float est = cap * (1.0f + noise);
            if (nTop < 24) {
                top[nTop++] = {est, cell};
            } else {
                int worst = 0;
                for (int k = 1; k < 24; k++)
                    if (top[k].est < top[worst].est) worst = k;
                if (est > top[worst].est) top[worst] = {est, cell};
            }
        }
    // Room is priced only here, on the two dozen finalists: the scan for it
    // is far too costly to run over every cell in a search radius. A site
    // hemmed in by other people's claims is worth what its gap allows, so
    // open country outbids a gap of the same soil, and the frontier fills
    // before the spaces behind it.
    for (int k = 0; k < nTop; k++) {
        float room = std::min(roomKm(pf, cellCentre(top[k].cell)), population::CLAIM_CAP_KM);
        if (room < population::CLAIM_FLOOR_KM) {
            top[k] = top[--nTop];
            k--;
            continue;
        }
        // Priced by the claim its takers would actually make, not by all the
        // room there is: valuing a target at the largest claim anyone could
        // ever hold made every prospect outbid home three to one, and whole
        // settlements marched off rather than sending colonists.
        top[k].est *= claimFactor(
            std::min(wantedReachKm(pf, top[k].cell, movers, population::SUSTAIN_R), room));
    }
    while (nTop > 0) {
        int bi = 0;
        for (int k = 1; k < nTop; k++)
            if (top[k].est > top[bi].est) bi = k;
        int cell = top[bi].cell;
        float scar = population::cellCondition(pf, cell, now);
        if (scar * top[bi].est >= population::MIN_SETTLEMENT_K) {
            if (estOut) *estOut = top[bi].est * scar; // the rumour, not the truth
            return cell;
        }
        top[bi] = top[--nTop];
    }
    return -1;
}

// A band on the march is people, one dot each: a column of forty strung out
// over forty or fifty metres of ground. Beyond a couple of hundred the dots
// stop being countable and the cap only bounds the drawing cost.
inline int bandDots(float P) {
    int n = (int)(P + 0.5f);
    return n < 3 ? 3 : (n > 200 ? 200 : n);
}
inline float bandSpreadKm(float P) { return 0.012f + 0.004f * std::sqrt((float)bandDots(P)); }

// A band forages the cell it stands on: same famine rule as a settlement, but
// a moving band gathers on a third of the day and carries only a small store.
// No growth on the march; a migration is months, not generations.
// What a band can drink where it stands, in people supported per day. The
// yield maps are land only, so open sea is zero and needs no rule of its
// own; a lake or a river is as much as anyone can drink, and ice is water
// you have to melt but water all the same. Salt is the distinction that
// matters -- without it the Great Lakes would be as deadly as the Atlantic.
inline float drinkableAt(const population::Field& pf, const hydrology::Result& hy,
                         const atmosphere::Climatology& clim, terrain::V3 n, int cell, double now,
                         float need) {
    bool lake = hy.cells[cell].lakeLevel > hydrology::NO_LAKE + 1.0f;
    bool sea = hy.heightM[cell] <= 0 && !lake;
    if (lake) return need; // fresh, and more of it than anyone can drink
    if (sea) {
        // Sea ice is fresh once it has aged, and a frozen surface can be
        // melted; open salt water cannot be drunk at all.
        float t = seasonalT(clim, n, 0.0f, now);
        return t < FROZEN_T ? need : 0.0f;
    }
    // On land, kWaterMap counts what the rivers carry -- which is the right
    // measure for a settlement of hundreds drawing every day, and the wrong
    // one for a band walking through. Where rain falls there is water in
    // pools, seeps andsmall streams below this grid, and nobody dies of thirst
    // in a rainforest for want of a river. Dry country is what kills.
    float rain = clim.rainMmDay.empty()
                     ? 2.0f
                     : atmosphere::seasonalAt(clim.rainMmDay, atmosphere::climFuzz(n), now);
    float wet = std::clamp((rain - 0.3f) / 0.7f, 0.0f, 1.0f);
    return std::max(pf.kWaterMap[cell], wet * need);
}

inline void integrateBand(population::Band& b, float flowBase, double span, bool resting,
                          const atmosphere::Climatology& clim, terrain::V3 n, float h,
                          double startT, float drinkable, float thirst) {
    float lat = std::asin(std::clamp(n.z, -1.0f, 1.0f));
    float lon = std::atan2(n.y, n.x);
    int steps = std::clamp((int)(span / 2.0) + 1, 1, 60);
    float dt = (float)(span / steps);
    float gather = resting ? population::GATHER_SETTLED : population::GATHER_MOVING;
    for (int k = 0; k < steps && dt > 0; k++) {
        double tk = startT + (k + 0.5) * dt;
        // A migrating band is never content: full firelight extension.
        float wh = daylight::workHours(lat, tk, 1.0f);
        float flow = flowBase * atmosphere::forageFactor(atmosphere::seasonalTempC(clim, n, h, tk));
        float H = std::min(flow, gather * b.P * wh / 12.0f);
        float cap = population::CAP_DAYS_BAND * std::max(b.P, 1.0f);
        float fill = std::clamp(b.S / cap, 0.0f, 1.0f);
        float excl = std::clamp(1.0f - fill / population::HOARD_FILL, 0.0f, 1.0f);
        float shortfall = b.P > 0 ? std::clamp(1.0f - H / b.P, 0.0f, 1.0f) : 0.0f;
        double a = startT + k * (double)dt;
        float act = dt >= 1.0f ? 1.0f : (float)(daylight::activeDays(lon, a, a + dt, wh) / dt);
        population::bandStarve(b.pop, population::STARVE_MAX * b.P * excl * shortfall * dt);
        b.P = b.pop.total();
        b.S = std::clamp(b.S + (H - b.P) * dt * act, 0.0f,
                         population::CAP_DAYS_BAND * std::max(b.P, 1.0f));
        // Water: drink what is here, carry away the surplus, spend the rest
        // out of the skins. Dry, and the ground giving nothing, kills fast.
        // The skins hold what they hold; in the heat that is fewer days.
        float wCap = population::CAP_WATER_DAYS * std::max(b.P, 1.0f);
        float need = b.P * thirst; // fewer mouths, less water, same heat
        b.water = std::clamp(b.water + (drinkable - need) * dt, 0.0f, wCap);
        if (b.water <= 0.0f && drinkable < need) {
            float dry = std::clamp(1.0f - drinkable / std::max(need, 1.0f), 0.0f, 1.0f);
            population::bandStarve(b.pop, population::THIRST_DEATH_RATE * b.P * dry * dt);
            b.P = b.pop.total();
        }
    }
}

// A band that arrives (or gives up) becomes a settlement on unclaimed ground.
inline void foundSettlement(population::Field& pf, technology::WorldState& ws,
                            const hydrology::Result& hy, const atmosphere::Climatology& clim,
                            const population::Band& b, int cell, double now) {
    population::Settlement s =
        population::newSettlement(cell, b.pop, b.P, population::cellCondition(pf, cell, now), now);
    // A community that picked up and moved keeps its identity along with
    // its name; only colonists are somebody new.
    s.id = b.sid ? b.sid : pf.nextSettlementId++;
    s.culture = b.culture;
    s.aff = b.aff;
    // A name follows a community, not a site: people who picked up and
    // moved are still themselves, while colonists who split away name the
    // place they have made their own.
    if (b.colonists && b.culture < pf.cultures.size())
        technology::uniqueName(pf, pf.cultures[b.culture], ws.rng, s.name);
    else
        for (int i = 0; i < 16; i++) s.name[i] = b.name[i];
    s.founded = now;
    pf.scars.erase(cell); // the land's condition is live state again
    for (int i = (int)pf.ruins.size() - 1; i >= 0; i--)
        if (pf.ruins[i].cell == cell) pf.ruins.erase(pf.ruins.begin() + i); // rebuilt over
    s.bows = b.bows;                                                        // carried on the march
    s.gRegion = population::gameRegion(cell);
    s.gameNow = pf.gameG.empty() ? 1.0f : pf.gameG[s.gRegion];
    s.sFarm = pf.sFarmMap[cell];
    s.sFish = pf.sFishMap[cell];
    s.pasture = pf.pastureMap[cell];
    s.buildMat = pf.buildMatMap[cell];
    s.sWood = pf.sWoodMap[cell];
    // The first days on new ground go to firewood before anything else
    // stands: a month's pile, not a winter's -- arriving in autumn on bare
    // tundra is as dangerous as it sounds.
    s.fuelS = 100.0f * b.P;
    s.cycleT = now; // the fill cycle starts with the settlement
    s.claimT = now; // and so does the frontier
    // They claim what they need on the day they arrive, as far as the room
    // allows: a community that walked here with five hundred people does not
    // wait a century to work enough ground to feed them.
    float take = std::min(wantedReachKm(pf, cell, s.P, population::SUSTAIN_R),
                          std::min(roomKm(pf, cellCentre(cell)), population::CLAIM_CAP_KM));
    take = std::max(take, population::CLAIM_FLOOR_KM);
    for (int k = 0; k < population::CLAIM_SECTORS; k++) s.claim[k] = take;
    applyClaim(pf, s);
    updateFarmland(pf, s);
    s.S = std::min(b.S, population::CAP_DAYS_SETTLED * b.P);
    for (int t = 0; t < population::NTECH; t++) s.tech[t] = b.tech[t];
    if (s.tech[population::TECH_HUSBANDRY].practising) {
        // Herders arrive with stock driven along the march, not a bare
        // seed: enough to feed a share of the arrivals while it grows.
        float hExp = technology::expertise(s.tech[population::TECH_HUSBANDRY], now);
        s.herd = std::max(technology::HERD_SEED, 0.25f * b.P * hExp);
    }
    atmosphere::seasonProfile(clim, cellCentre(cell), std::max(hy.heightM[cell], 0.0f), s.tSeason,
                              s.meanF, s.meanG2);
    int idx = (int)pf.settlements.size();
    pf.settlementAt[cell] = idx;
    pf.settlements.push_back(s);
    pf.neighbours.push_back({});
    terrain::V3 n = cellCentre(cell);
    for (int j = 0; j < idx; j++)
        if (distKm(n, cellCentre(pf.settlements[j].cell)) <= population::CONTACT_KM) {
            pf.neighbours[idx].push_back(j);
            pf.neighbours[j].push_back(idx);
        }
    for (int t = 0; t < population::NTECH; t++) {
        technology::redraw(pf, idx, ws, t, now);
        for (int j : pf.neighbours[idx]) technology::redraw(pf, j, ws, t, now);
        technology::scheduleInvention(pf, ws, t, now);
    }
    {
        char txt[96];
        int km = b.fromCell >= 0 ? (int)distKm(cellCentre(b.fromCell), cellCentre(cell)) : 0;
        if (b.colonists)
            snprintf(txt, sizeof txt, "%s was founded by colonists from %s, %d km away", s.name,
                     b.name, km);
        else
            snprintf(txt, sizeof txt, "%s settled again %d km from their old home", s.name, km);
        note(pf, b.colonists ? population::EV_FOUNDED : population::EV_SETTLED, now, s.id, 0, 0,
             b.P, txt);
    }
    logAt("founded settlement", idx, n, b.P, now);
}

// Merge a failing band into the nearest settlement in reach; what it knows
// travels with it.
inline bool mergeBand(population::Field& pf, technology::WorldState& ws, const population::Band& b,
                      double now) {
    terrain::V3 n = {b.px, b.py, b.pz};
    int ti = -1;
    float td = population::CONTACT_KM;
    for (int i = 0; i < (int)pf.settlements.size(); i++) {
        if (pf.settlements[i].leaving) continue; // that place is being abandoned
        float d = distKm(n, cellCentre(pf.settlements[i].cell));
        if (d < td) {
            td = d;
            ti = i;
        }
    }
    if (ti < 0) return false;
    population::Settlement& t = pf.settlements[ti];
    t.pop.add(b.pop);
    t.P = t.pop.total();
    t.bows += b.bows;
    t.S = std::min(t.S + b.S, population::CAP_DAYS_SETTLED * t.P);
    for (int tc = 0; tc < population::NTECH; tc++) {
        if (b.tech[tc].aware && !t.tech[tc].aware) {
            t.tech[tc].aware = true;
            technology::redraw(pf, ti, ws, tc, now);
            for (int j : pf.neighbours[ti])
                if (!pf.settlements[j].tech[tc].aware) technology::redraw(pf, j, ws, tc, now);
            technology::scheduleInvention(pf, ws, tc, now);
        }
        if (b.tech[tc].practising && !t.tech[tc].practising) {
            t.tech[tc].practising = true;
            t.tech[tc].practiceT = b.tech[tc].practiceT;
            t.nextTech[tc] = 1e18;
            if (tc == population::TECH_HUSBANDRY && t.herd <= 0) t.herd = technology::HERD_SEED;
            if (tc == population::TECH_FARMING)
                technology::redraw(pf, ti, ws, population::TECH_GRANARY, now);
            for (int j : pf.neighbours[ti]) technology::redraw(pf, j, ws, tc, now);
            technology::scheduleInvention(pf, ws, tc, now);
        }
    }
    {
        char txt[96];
        snprintf(txt, sizeof txt, "%s gave up the road and joined %s", b.name, t.name);
        note(pf, population::EV_MERGED, now, t.id, 0, b.id, b.P, txt);
    }
    logAt("merged into settlement", ti, n, b.P, now);
    return true;
}

// One band re-evaluation: integrate, move, then decide â€” rest, settle here,
// arrive, re-target, or give up. Returns false if the band no longer exists.
inline bool stepBand(population::Field& pf, technology::WorldState& ws, const hydrology::Result& hy,
                     const atmosphere::Climatology& clim, int bi, double now) {
    population::Band& b = pf.bands[bi];
    terrain::V3 pos = {b.px, b.py, b.pz};
    double span = now - b.t;
    b.t = now;
    int hereCell = cellOf(pos);
    double startT = b.t - span; // b.t was already moved to now
    // 0 on water: crossings cost stores. The game-borne share of the cell's
    // yield follows the regional pool's health, like a settlement's does.
    float flowBase = pf.K[hereCell] * population::SUSTAIN_R;
    if (flowBase > 0 && pf.kFoodPMap[hereCell] > 0 && !pf.gameG.empty()) {
        float g = pf.gameG[population::gameRegion(hereCell)];
        float scale =
            (pf.kFoodPMap[hereCell] - pf.kGameMap[hereCell] * (1.0f - population::huntEff(g))) /
            pf.kFoodPMap[hereCell];
        flowBase *= std::max(scale, 0.0f);
    }
    // Thirst rises with the heat, so the same skins go less far in the south.
    float hHere0 = std::max(hy.heightM[hereCell], 0.0f);
    float thirst = population::thirstFactor(seasonalT(clim, pos, hHere0, now - span * 0.5));
    integrateBand(b, flowBase, span, b.resting, clim, pos, hHere0, startT,
                  drinkableAt(pf, hy, clim, pos, hereCell, now - span * 0.5, b.P * thirst), thirst);
    if (b.P < population::BAND_MIN_P) {
        if (!mergeBand(pf, ws, b, now)) logAt("perished", bi, pos, b.P, now);
        pf.bands.erase(pf.bands.begin() + bi);
        return false;
    }
    // A raiding party has somewhere to be and does not settle en route:
    // outward to its mark, then home with whatever it took.
    if (b.purpose == population::BAND_RAID) {
        int hi = population::indexById(pf, b.homeId);
        if (!b.returning) {
            int ti = population::indexById(pf, b.targetId);
            if (ti < 0)
                b.returning = true; // they moved on; nothing to rob
            else {
                b.targetCell = pf.settlements[ti].cell;
                if (distKm(pos, cellCentre(b.targetCell)) < 20.0f) resolveRaid(pf, ws, b, ti, now);
            }
        }
        if (b.returning) {
            if (hi < 0) { // home is gone: join whoever will have them
                if (!mergeBand(pf, ws, b, now)) logAt("perished", bi, pos, b.P, now);
                pf.bands.erase(pf.bands.begin() + bi);
                return false;
            }
            b.targetCell = pf.settlements[hi].cell;
            if (distKm(pos, cellCentre(b.targetCell)) < 20.0f) {
                population::Settlement& h = pf.settlements[hi];
                h.pop.add(b.pop); // the survivors, back among their people
                h.aff.fight = std::max(h.aff.fight, b.aff.fight);
                h.P = h.pop.total();
                h.bows += b.bows;
                h.herd += b.lootHerd;
                h.S = std::min(h.S + b.S + b.loot,
                               population::storageCapDays(h.P, h.granaries) * h.P);
                {
                    char txt[96];
                    snprintf(txt, sizeof txt, "%s raiders came home with %d rations, %d livestock",
                             b.name, (int)b.loot, (int)b.lootHerd);
                    note(pf, population::EV_RAID_HOME, now, h.id, 0, 0, b.loot, txt);
                }
                logAt("raiders home to settlement", hi, pos, b.P, now);
                pf.bands.erase(pf.bands.begin() + bi);
                return false;
            }
        }
    }

    terrain::V3 tgt = cellCentre(b.targetCell);
    if (!b.resting) {
        // Terrain under our feet sets the pace: rafting is slow, ice walks,
        // a major unfrozen river means fording. Light sets the hours: bands
        // walk while there is light to walk by (through civil twilight),
        // sleep the rest -- 15 km/day is the 12-lit-hour baseline. Sub-day
        // steps show it: a band stands still in the dead of night.
        float lat = std::asin(std::clamp(pos.z, -1.0f, 1.0f));
        float lonB = std::atan2(pos.y, pos.x);
        float lh = daylight::travelHours(lat, now - span * 0.5);
        float km = population::BAND_SPEED_KM_DAY / 12.0f * lh *
                   (float)daylight::activeDays(lonB, now - span, now, lh);
        float factor = 1.0f;
        bool water =
            hy.heightM[hereCell] <= 0 || hy.cells[hereCell].lakeLevel > hydrology::NO_LAKE + 1;
        float hHere = std::max(hy.heightM[hereCell], 0.0f);
        bool frozen = seasonalT(clim, pos, hHere, now) < FROZEN_T;
        if (water && !frozen)
            factor *= RAFT_FACTOR;
        else if (!water && !frozen && hy.cells[hereCell].flow >= RIVER_MAJOR_KM2)
            factor *= RIVER_CROSS_FACTOR;
        pos = moveToward(pos, tgt, km * factor);
        b.px = pos.x;
        b.py = pos.y;
        b.pz = pos.z;
    }
    int cell = cellOf(pos);
    if (b.purpose == population::BAND_RAID) { // no resting, no founding: keep marching
        b.nextUpdate = now + population::BAND_STEP_DAYS;
        return true;
    }
    float fill = b.S / (population::CAP_DAYS_BAND * std::max(b.P, 1.0f));
    if (b.resting) {
        if (fill >= 0.95f || now - b.restStart > 90.0) b.resting = false;
    } else if (fill < 0.3f && pf.K[cell] * population::SUSTAIN_R > b.P) {
        b.resting = true;
        b.restStart = now;
    }
    bool done = false;
    // The band's skills decide what ground is worth settling (moverCap):
    // herders take steppe a forager would starve on. And ground is only
    // worth settling if it can feed the people who would settle it -- the
    // same test a settlement applies when deciding whether to stay. Without
    // it a group that left because the valley could not feed three hundred
    // would happily re-found on that same valley the next day: leaving was
    // judged against its population, settling against a fixed threshold.
    float fExp = technology::expertise(b.tech[population::TECH_FARMING], now);
    float hExp = technology::expertise(b.tech[population::TECH_HUSBANDRY], now);
    float qExp = technology::expertise(b.tech[population::TECH_FISHING], now);
    auto canHold = [&](int c) { return moverCapRoom(pf, c, fExp, hExp, now, b.P, qExp) >= b.P; };
    if (distKm(pos, tgt) < 20.0f) {
        // Arrived: the rumour meets reality.
        if (pf.settlementAt[cell] < 0 &&
            moverCap(pf, cell, fExp, hExp, now, qExp, b.P) >= population::MIN_SETTLEMENT_K &&
            canHold(cell) && claimFits(pf, pos)) {
            foundSettlement(pf, ws, hy, clim, b, cell, now);
            done = true;
        } else {
            double rest = b.resting ? now - b.restStart : 0.0;
            int nt = bestProspect(pf, pos, ws.rng,
                                  population::bandAwareKm(rest, prominenceM(hy, clim, cell)), now,
                                  fExp, hExp, nullptr, b.P, qExp);
            if (nt >= 0)
                b.targetCell = nt;
            else {
                if (!mergeBand(pf, ws, b, now)) logAt("perished", bi, pos, b.P, now);
                done = true;
            }
        }
    } else if (!b.resting && pf.settlementAt[cell] < 0 &&
               moverCap(pf, cell, fExp, hExp, now, qExp, b.P) >= population::MIN_SETTLEMENT_K &&
               claimFits(pf, pos) && canHold(cell) &&
               moverCapRoom(pf, cell, fExp, hExp, now, b.P, qExp) >=
                   population::hopeRatio(now - b.setOut) *
                       moverCapRoom(pf, b.targetCell, fExp, hExp, now, b.P, qExp)) {
        // Ground under their feet, judged against where they were going:
        // early on it has to be clearly better to be worth giving up the
        // plan, and as the months pass they grow readier to take less.
        foundSettlement(pf, ws, hy, clim, b, cell, now);
        done = true;
    }
    if (done) {
        pf.bands.erase(pf.bands.begin() + bi);
        return false;
    }
    b.nextUpdate = now + population::BAND_STEP_DAYS;
    return true;
}

// Sustained scarcity forces a choice, and the default answer is to move as
// a whole: people are kin, and a place that has failed fails for everyone,
// so the group first looks for ground that can carry all of them. Fission
// is the FALLBACK -- what you do when the world has no room left for the
// whole group -- which is why the colonization wave appears only as the map
// fills. Sunk investment anchors the choice: every granary and every year of
// cleared field raises the bar a destination must clear, so foragers and
// herders shift readily while a farming village splits and stays put.
inline void maybeRelocateOrSplit(population::Field& pf, technology::WorldState& ws,
                                 const hydrology::Result& hy, const atmosphere::Climatology& clim,
                                 int si, double now) {
    population::Settlement& s = pf.settlements[si];
    if (s.leaving) return;
    float keff = technology::effectiveK(s, now);
    float phi = s.P > 1 ? keff * s.R / s.P : 2.0f;
    // Food-limited: a group with every calorie need met grows at its
    // maximum rate, and growth saturates at PHI_CONTENT. Anything short of
    // that means food is what is holding them back -- reason enough to look
    // for somewhere else, long before the place is visibly failing. (The
    // burial term is the backstop for what the annual mean cannot see: a
    // sharply seasonal site can read comfortable on the year while the lean
    // season still kills, and people dying of hunger is food limiting
    // growth in the plainest possible sense.)
    bool bleeding = s.starvedYr > population::STARVE_NOTICE * std::max(s.P, 1.0f);
    bool foodLimited = phi < population::PHI_CONTENT || bleeding;
    // Sustained hunger for need-driven invention: a genuine shortfall
    // (phi < NEED_HUNGRY_PHI), not the comfort glide. Checked before every
    // early return so small settlements get desperate too; leaving or
    // splitting does not reset it -- neither cures desperation by itself.
    if (phi >= population::NEED_HUNGRY_PHI && !bleeding)
        s.hungrySince = -1;
    else if (s.hungrySince < 0)
        s.hungrySince = now;
    if (!foodLimited) {
        s.scarceSince = -1;
        s.noProspect = false;
        return;
    }
    // A genuinely hungry settlement must wake in time to ask whether to
    // leave: its ordinary horizon can be years long, and famine would then
    // resolve the crisis mid-sleep -- starving down to fit rather than
    // moving, with the question never asked (seen in testing: asleep 1,383
    // days through its own 730-day deadline). Only real hunger earns the
    // early wake; at the ordinary equilibrium glide every settlement is
    // nominally scarce, and re-deciding the whole world every two years
    // costs far more than it is worth.
    bool starving = phi < population::NEED_HUNGRY_PHI || bleeding;
    if (s.scarceSince < 0) {
        s.scarceSince = now;
        if (starving) s.nextUpdate = std::min(s.nextUpdate, now + population::SPLIT_AFTER_DAYS);
        return;
    }
    // Never schedule into the past: a deadline that has already gone by
    // would be re-popped from the queue forever (the event time would keep
    // matching), rewinding the settlement's clock instead of advancing it.
    // Real hunger restores the short cadence: things got worse, so they
    // look again in earnest.
    if (phi < population::NEED_HUNGRY_PHI || bleeding)
        s.lookAgainDays = population::SPLIT_AFTER_DAYS;
    if (starving)
        s.nextUpdate = std::min(s.nextUpdate, std::max(s.scarceSince + s.lookAgainDays, now + 5.0));
    if (now - s.scarceSince < s.lookAgainDays) return;
    s.scarceSince = now; // whether or not anyone leaves, the pressure resets
    if (starving) s.nextUpdate = std::min(s.nextUpdate, now + s.lookAgainDays);
    // Hunger widens the border before it empties the village. People range
    // further from the houses they have long before they abandon them, so a
    // settlement with anywhere left to widen works the ground it just took
    // and asks again later. Only when the claim can grow no further -- the
    // cap reached, or neighbours on every side -- does anybody leave. This
    // is what makes a border worth having, and being hemmed in the thing
    // that turns pressure into a journey and eventually into a fight.
    if (growClaim(pf, si, now)) return;
    if (s.P < population::BAND_MIN_P) return; // too few to survive any journey
    float fExp = technology::expertise(s.tech[population::TECH_FARMING], now);
    float hExp = technology::expertise(s.tech[population::TECH_HUSBANDRY], now);
    float qExp = technology::expertise(s.tech[population::TECH_FISHING], now);
    terrain::V3 home = cellCentre(s.cell);
    float est = 0;
    int tgt =
        bestProspect(pf, home, ws.rng,
                     population::settlementAwareKm(now - s.founded, prominenceM(hy, clim, s.cell)),
                     now, fExp, hExp, &est, s.P, qExp);
    bool wasStuck = s.noProspect; // the last survey came up empty too
    s.noProspect = tgt < 0;
    if (tgt < 0) {
        // Circumscription: hemmed in, hungry, and nowhere to go. This is
        // where raiding comes from -- but only once it is clear there is no
        // land to be had, not on the first disappointing look around.
        bool raided = wasStuck && maybeRaid(pf, ws, si, now);
        s.lookAgainDays = std::min(s.lookAgainDays * 2.0, population::LOOK_BACKOFF_MAX);
        (void)raided;
        return;
    }

    // Judged on the rumour, not the truth: a group deciding whether to pick
    // up and leave knows only what it has heard, and distant ground is
    // reported optimistically as often as not. Arriving to a poorer valley
    // than promised is a real outcome -- the band re-evaluates on arrival
    // against this same measure, so nobody marches toward ground they would
    // refuse when they got there.
    float targetSupport = est * population::SUSTAIN_R;
    float homeSupport = keff * s.R; // what this place carries in its present state
    float anchor = 1.0f + population::RELOC_ANCHOR_GRANARY * s.granaries +
                   population::RELOC_ANCHOR_FARM * fExp +
                   population::RELOC_ANCHOR_FSTEAD * s.farmsteads;
    bool wholeGroup = targetSupport >= s.P && targetSupport >= anchor * homeSupport;
    if (!wholeGroup &&
        s.P < population::SPLIT_MIN_P) { // nowhere for all, too few to divide: endure
        s.lookAgainDays = std::min(s.lookAgainDays * 2.0, population::LOOK_BACKOFF_MAX);
        return;
    }
    s.lookAgainDays = population::SPLIT_AFTER_DAYS; // something came of it: keep looking

    population::Band b{};
    b.id = pf.nextBandId++;
    b.px = home.x;
    b.py = home.y;
    b.pz = home.z;
    b.culture = s.culture;
    b.aff = s.aff;
    b.colonists = !wholeGroup; // a splinter, not the town on the move
    for (int i = 0; i < 16; i++) b.name[i] = s.name[i];
    b.pop = s.pop;
    if (!wholeGroup) b.pop.scale(population::SPLIT_SHARE);
    b.P = b.pop.total();
    b.S = std::min((wholeGroup ? s.S : s.S * population::SPLIT_SHARE),
                   population::CAP_DAYS_BAND * b.P);
    b.water = population::CAP_WATER_DAYS * b.P; // nobody sets out with empty skins
    b.bows = wholeGroup ? s.bows : s.bows * population::SPLIT_SHARE; // people take their bows
    b.targetCell = tgt;
    b.setOut = now;
    b.fromCell = s.cell;
    b.t = now;
    b.nextUpdate = now + population::BAND_STEP_DAYS;
    for (int t = 0; t < population::NTECH; t++) b.tech[t] = s.tech[t];
    if (wholeGroup) {
        b.sid = s.id; // they are still themselves, wherever they end up
        // The land remembers what it was left in; only a place that was
        // invested in leaves anything to find.
        population::markScar(pf, s.cell, s.R, now, s.id);
        if (s.granaries >= 1.0f || s.farmsteads >= 1.0f ||
            now - s.founded > population::RUIN_MIN_AGE_DAYS) {
            population::Field::Ruin r{s.cell, now, {}};
            for (int i = 0; i < 16; i++) r.name[i] = s.name[i];
            pf.ruins.push_back(r);
        }
        s.leaving = true; // swept once the step's events are done
        // Free the ground now rather than at the sweep: otherwise how long a
        // vacated site stays blocked depends on how big a time step the
        // player happens to take, which is exactly what this simulation is
        // not allowed to do (a 500-year step held every site for 500 years;
        // ten-year steps freed them within one).
        if (pf.settlementAt[s.cell] == si) pf.settlementAt[s.cell] = -1;
        // Every last person is in the band now. Clearing the headcount is
        // not enough: advance() recomputes P from the cohorts, so a record
        // that keeps its cohorts is a settlement that comes back to life on
        // the next wake -- with its knowledge, its hunger and its vote in
        // the world's inventions.
        s.pop = population::Cohorts{};
        s.P = 0;
        s.S = 0;
        s.herd = 0;
        s.nextUpdate = 1e18;
        for (int t = 0; t < population::NTECH; t++) s.nextTech[t] = 1e18;
        {
            char txt[96];
            snprintf(txt, sizeof txt, "%s abandoned their home and set out", s.name);
            note(pf, population::EV_RELOCATE, now, s.id, 0, b.id, b.P, txt);
        }
        logAt("settlement moves on", si, home, b.P, now);
    } else {
        s.pop.sub(b.pop);
        s.P = s.pop.total();
        s.S -= b.S;
        s.bows -= b.bows;
        {
            char txt[96];
            snprintf(txt, sizeof txt, "%d colonists left %s", (int)b.P, s.name);
            note(pf, population::EV_SPLIT, now, s.id, 0, b.id, b.P, txt);
        }
        logAt("split from settlement", si, home, b.P, now);
    }
    pf.bands.push_back(b);
}

} // namespace sim
