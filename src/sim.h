// The simulation orchestrator: processes settlement re-evaluations, technology
// events, splits, and band journeys in strict chronological order, since each
// event can change the rates of the others. See Design/Migration.md.
#pragma once
#include "technology.h"
#include "atmosphere.h"
#include <cmath>
#include <cstdio>
#include <queue>

namespace sim {

// Passability (Design/Migration.md): nothing blocks outright. Unfrozen open
// water is crossed at half speed (rafts); frozen water is walked at full
// speed (winter is the crossing season -- ice-bridge migrations); a major
// unfrozen river slows a band to fording pace for the cell it crosses.
constexpr float FROZEN_T = -2.0f;         // degC, seasonal local temperature
constexpr float RAFT_FACTOR = 0.5f;
constexpr float RIVER_CROSS_FACTOR = 0.4f;
constexpr float RIVER_MAJOR_KM2 = 40000.0f; // runoff-equivalent area

// Prominence: how far the site rises above its regional (climate-grid) mean
// elevation. The vantage input to awareness.
inline float prominenceM(const hydrology::Result& hy, const atmosphere::Climatology& clim, int cell) {
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

inline terrain::V3 cellCentre(int cell) {
    hydrology::V3orig d = hydrology::cellDir(cell % population::W, cell / population::W);
    return {d.x, d.y, d.z};
}

inline int cellOf(terrain::V3 n) {
    float lat = std::asin(std::clamp(n.z, -1.0f, 1.0f));
    float lon = std::atan2(n.y, n.x);
    int cx = hydrology::wrapX((int)std::floor((lon + 3.14159265f) / (2 * 3.14159265f) * population::W));
    int cy = std::clamp((int)std::floor((lat + 3.14159265f / 2) / 3.14159265f * population::H), 0,
                        population::H - 1);
    return cy * population::W + cx;
}

// Haversine-style: acos(dot) loses about 3 km of precision on nearly equal
// unit vectors, which is fatal for the kilometre-scale tests (marker picking,
// arrival checks). The chord form stays exact all the way down to zero.
inline float distKm(terrain::V3 a, terrain::V3 b) {
    terrain::V3 d{a.x - b.x, a.y - b.y, a.z - b.z};
    float half = std::sqrt(terrain::dot(d, d)) * 0.5f;
    return 2.0f * std::asin(std::clamp(half, 0.0f, 1.0f)) * 6371.0f;
}

inline terrain::V3 norm3(terrain::V3 v) {
    float l = std::sqrt(terrain::dot(v, v));
    return {v.x / l, v.y / l, v.z / l};
}

// Great-circle step of `km` from p toward q.
inline terrain::V3 moveToward(terrain::V3 p, terrain::V3 q, float km) {
    float ang = std::acos(std::clamp(terrain::dot(p, q), -1.0f, 1.0f));
    float step = km / 6371.0f;
    if (ang <= step || ang < 1e-6f) return q;
    float t = step / ang, sa = std::sin(ang);
    return norm3(p * (std::sin((1 - t) * ang) / sa) + q * (std::sin(t * ang) / sa));
}

// Record something worth telling. Counts stay exact; the stored entries
// stop at a cap so a thousand-year step cannot eat memory.
inline void note(population::Field& pf, int kind, double t, uint32_t sid, uint32_t sid2,
                 uint32_t bandId, float amount, const char* text) {
    using namespace population;
    if (kind < 0 || kind >= EV_KINDS) return;
    pf.eventCount[kind]++;
    if (pf.eventCount[kind] > EVENTS_KEPT_PER_KIND) return;
    Event e;
    e.kind = (uint8_t)kind;
    e.t = t;
    // Note where it happened while the people involved are still findable:
    // they may be gone by the time anyone reads this.
    int at = indexById(pf, sid);
    if (at < 0) at = indexById(pf, sid2);
    if (at >= 0) e.cell = pf.settlements[at].cell;
    e.sid = sid;
    e.sid2 = sid2;
    e.bandId = bandId;
    e.amount = amount;
    for (int i = 0; i < 95 && text[i]; i++) e.text[i] = text[i];
    pf.events.push_back(e);
}

inline void logAt(const char* what, int id, terrain::V3 n, float P, double day) {
    float lat = std::asin(std::clamp(n.z, -1.0f, 1.0f)) * 180.0f / 3.14159265f;
    float lon = std::atan2(n.y, n.x) * 180.0f / 3.14159265f;
    fprintf(stderr, "band: %s %d at lat %.2f lon %.2f, %d people, day %.0f\n", what, id, lat, lon,
            (int)P, day);
}

// Any settlement within 80 km? Checked through the settlementAt grid: a box
// of +-5 rows (100 km) and enough columns for 80 km at this latitude covers
// every cell that could hold one; found entries are then measured exactly.
inline bool spacingOK(const population::Field& pf, terrain::V3 n) {
    using namespace population;
    int cell = cellOf(n), cx = cell % W, cy = cell / W;
    float lat = std::asin(std::clamp(n.z, -1.0f, 1.0f));
    int rx = std::min((int)std::ceil(4.0f / std::max(std::cos(lat), 0.05f)) + 1, W / 2);
    for (int dy = -5; dy <= 5; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= H) continue;
        for (int dx = -rx; dx <= rx; dx++) {
            int si = pf.settlementAt[y * W + hydrology::wrapX(cx + dx)];
            if (si >= 0 && distKm(n, cellCentre(pf.settlements[si].cell)) < 80.0f) return false;
        }
    }
    return true;
}

// The capacity a mover with these skills would command at a cell: forager
// yield plus the farming and herding bonuses for what it practises. This is
// how a herding band values the steppe a forager walks past -- and it must
// gate founding too, or a band would choose a target it then refuses.
inline float moverCap(const population::Field& pf, int cell, float farmExp, float husbExp,
                      double now) {
    using namespace population;
    float food = pf.kFoodPMap[cell];
    if (food <= 0) return 0;
    food += food * technology::FARM_YIELD_GAIN * pf.sFarmMap[cell] * farmExp;
    if (husbExp > 0)
        food += pf.pastureMap[cell] * FORAGE_KM2 * HERD_PASTURE_K / SUSTAIN_R *
                (0.3f + 0.7f * husbExp) * 0.85f;
    return std::min(food, pf.kWaterMap[cell]);
}

// The same, priced for land that has been lived on and left: an exhausted
// valley is a bad place to move to for a generation. Only applied to the
// finalists of a search -- scars are sparse, and the lookup is not free.
inline float moverCapScarred(const population::Field& pf, int cell, float farmExp, float husbExp,
                             double now) {
    return moverCap(pf, cell, farmExp, husbExp, now) * population::cellCondition(pf, cell, now);
}

// The best-looking unclaimed prospect within the knowledge range, judged with
// noise that grows with distance: near things resolve exactly, far things are
// rumours; each candidate is valued at what THIS mover could make of it.
// Returns a cell index, or -1 if nothing known is worth going to.
inline int bestProspect(const population::Field& pf, terrain::V3 from, uint64_t& rng,
                        float radiusKm, double now, float farmExp = 0, float husbExp = 0,
                        float* estOut = nullptr) {
    using namespace population;
    bool skilled = farmExp > 0 || husbExp > 0;
    float lat0 = std::asin(std::clamp(from.z, -1.0f, 1.0f));
    float dLat = radiusKm / 6371.0f;
    int y0 = std::max((int)(((lat0 - dLat) + 3.14159265f / 2) / 3.14159265f * H), 1);
    int y1 = std::min((int)(((lat0 + dLat) + 3.14159265f / 2) / 3.14159265f * H) + 1, H - 2);
    // Cheap scoring pass (chord distance, no spacing checks), then the
    // expensive spacing check only on the best few in score order.
    struct Cand { float est; int cell; };
    Cand top[24];
    int nTop = 0;
    for (int y = y0; y <= y1; y += 2)
        for (int x = 0; x < W; x += 2) {
            int cell = y * W + x;
            // Cheap exact rejections first -- this scan runs over thousands
            // of cells per search and every search is a settlement deciding
            // its future. Water caps any mover's capacity, and a mover with
            // no skills is worth exactly K.
            if (pf.kWaterMap[cell] < MIN_SETTLEMENT_K) continue;
            if (!skilled && pf.K[cell] < MIN_SETTLEMENT_K) continue;
            float cap = skilled ? moverCap(pf, cell, farmExp, husbExp, now) : pf.K[cell];
            if (cap < MIN_SETTLEMENT_K) continue;
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
    while (nTop > 0) {
        int bi = 0;
        for (int k = 1; k < nTop; k++)
            if (top[k].est > top[bi].est) bi = k;
        int cell = top[bi].cell;
        float scar = population::cellCondition(pf, cell, now);
        if (scar * top[bi].est >= MIN_SETTLEMENT_K && spacingOK(pf, cellCentre(cell))) {
            if (estOut) *estOut = top[bi].est * scar; // the rumour, not the truth
            return cell;
        }
        top[bi] = top[--nTop];
    }
    return -1;
}

// A granary finished during the last integration: advance() counts them,
// the simulation is what tells anyone about it.
inline void reportGranaries(population::Field& pf, population::Settlement& s, double now) {
    while (s.builtGranaries > 0) {
        s.builtGranaries--;
        char txt[96];
        snprintf(txt, sizeof txt, "%s finished a granary (%d standing)", s.name,
                 (int)s.granaries);
        note(pf, population::EV_GRANARY, now, s.id, 0, 0, s.granaries, txt);
    }
}

// Advance every regional game pool to `now` (population.h constants). The
// draw is what the region's settlements currently eat from the game side of
// their diet; depletion is proportional to actual kills, recovery is slow,
// and below the Allee floor there is no recovery at all. Runs on a fixed
// 90-day schedule (plus catch-up), so it is step-size invariant. Bands are
// too small and transient to count.
inline void gameTick(population::Field& pf, double now) {
    using namespace population;
    double dt = now - pf.gameT;
    if (dt <= 0 || pf.gameG.empty()) return;
    std::vector<float> draw(pf.gameG.size(), 0.0f);
    for (const Settlement& s : pf.settlements) {
        if (s.kGame <= 0 || s.P <= 1) continue;
        float g = pf.gameG[s.gRegion];
        float archExp = technology::expertise(s.tech[TECH_ARCHERY], now);
        float cover = bowCoverage(s.bows, s.P);
        float gameFlow =
            s.kGame * huntEff(g) * (1.0f + BOW_BIG_GAIN * cover * archExp) * s.meanF;
        float farmMult = 1.0f + technology::FARM_YIELD_GAIN * s.sFarm *
                                    technology::expertise(s.tech[TECH_FARMING], now);
        float hExp = technology::expertise(s.tech[TECH_HUSBANDRY], now);
        float total = (s.kFoodP - s.kGame - s.kSmall) * s.meanF +
                      s.kSmall * smallGameEff(cover, archExp) * s.meanF + gameFlow +
                      s.kFoodP * (farmMult - 1.0f) + s.herd * 0.85f +
                      FARMYARD_SHARE_POP * s.kFoodP * hExp;
        if (total <= 1e-6f) continue;
        draw[s.gRegion] += s.P * gameFlow / total; // game share of what they eat
    }
    for (size_t r = 0; r < pf.gameG.size(); r++) {
        if (pf.gameDmax[r] <= 0) continue;
        float g = pf.gameG[r];
        float regen = g >= GAME_FLOOR ? (1.0f - g) / (GAME_REGEN_YEARS * 365.0f) : 0.0f;
        float depl = draw[r] / pf.gameDmax[r] / (GAME_DEPLETE_YEARS * 365.0f);
        float before = g;
        pf.gameG[r] = std::clamp(g + (regen - depl) * (float)dt, 0.0f, 1.0f);
        if (before >= GAME_FLOOR && pf.gameG[r] < GAME_FLOOR)
            note(pf, EV_GAME_GONE, now, 0, 0, 0, 0,
                 "a regional herd was hunted past saving");
    }
    for (Settlement& s : pf.settlements)
        if (s.kGame > 0) s.gameNow = pf.gameG[s.gRegion];
    pf.gameT = now;
}

inline population::SeasonCtx seasonCtx(const population::Settlement& s,
                                       const hydrology::Result& hy,
                                       const atmosphere::Climatology& clim, double now) {
    population::SeasonCtx ctx;
    ctx.clim = &clim;
    ctx.n = cellCentre(s.cell);
    ctx.h = std::max(hy.heightM[s.cell], 0.0f);
    ctx.farmMult = 1.0f + technology::FARM_YIELD_GAIN * s.sFarm *
                       technology::expertise(s.tech[population::TECH_FARMING], now);
    ctx.husbExp = technology::expertise(s.tech[population::TECH_HUSBANDRY], now);
    ctx.granExp = technology::expertise(s.tech[population::TECH_GRANARY], now);
    ctx.gameG = s.gameNow;
    ctx.archExp = technology::expertise(s.tech[population::TECH_ARCHERY], now);
    ctx.bowCover = population::bowCoverage(s.bows, s.P);
    ctx.aff = s.aff;
    return ctx;
}

// Granary marker positions: a ring of small structures around the
// settlement's cell centre, spaced by the golden angle with a per-cell
// integer phase. Defined ONCE here and mirrored exactly in
// shaders/globe.frag (granaryNear) so drawing and the tooltip cannot drift.
constexpr float GRANARY_RING_KM = 6.5f;
inline terrain::V3 granaryPos(int cell, int k) {
    terrain::V3 c = cellCentre(cell);
    terrain::V3 east = norm3({-c.y, c.x, 0.0f});
    terrain::V3 north = {c.y * east.z - c.z * east.y, c.z * east.x - c.x * east.z,
                         c.x * east.y - c.y * east.x};
    float a = 2.39996f * k + (float)(cell % 628) * 0.01f;
    float r = GRANARY_RING_KM / 6371.0f;
    return norm3(c + (east * std::cos(a) + north * std::sin(a)) * r);
}

// Fields around a settlement. Stone tools did not stop the first farmers
// clearing woodland -- the axe girdles, the fire does the work, and the ash
// manures the first crop -- so the mark a farming village leaves is a
// clearing, not a patch of open ground it happened to find. Area: roughly
// 0.4 ha under crop feeds a person at Neolithic yields, and with a long
// fallow (a rotation of some twenty years) the land inside the rotation is
// about eight hectares a head. That whole mosaic -- crop, stubble, scrub
// regrowth, the trees not yet taken -- is what farming looks like from
// above, so it is the footprint drawn.
constexpr float FARM_HA_PER_FARMER = 8.0f;
constexpr float VILLAGE_KM = 0.8f; // the houses; fields ring them, not cover them

// How far the fields reach: the outer edge of an annulus around the village
// holding the area this settlement's farmers work. Zero for anyone who does
// not farm, so it is also the test for whether to draw fields at all.
inline float farmRadiusKm(const population::Settlement& s, double now) {
    float fExp = technology::expertise(s.tech[population::TECH_FARMING], now);
    if (fExp <= 0 || s.sFarm <= 0 || s.P <= 0) return 0.0f;
    float k = technology::effectiveK(s, now);
    if (k <= 0) return 0.0f;
    // The share of the settlement's food the fields provide, applied to its
    // people: the number of mouths the fields actually feed.
    float farmers = s.P * std::min(s.kFoodP * technology::FARM_YIELD_GAIN * s.sFarm * fExp / k, 1.0f);
    float areaKm2 = farmers * FARM_HA_PER_FARMER * 0.01f; // 100 ha to the km^2
    return std::sqrt(VILLAGE_KM * VILLAGE_KM + areaKm2 / 3.14159265f);
}

// A band forages the cell it stands on: same famine rule as a settlement, but
// a moving band gathers on a third of the day and carries only a small store.
// No growth on the march; a migration is months, not generations.
inline void integrateBand(population::Band& b, float flowBase, double span, bool resting,
                          const atmosphere::Climatology& clim, terrain::V3 n, float h,
                          double startT) {
    using namespace population;
    float lat = std::asin(std::clamp(n.z, -1.0f, 1.0f));
    float lon = std::atan2(n.y, n.x);
    int steps = std::clamp((int)(span / 2.0) + 1, 1, 60);
    float dt = (float)(span / steps);
    float gather = resting ? GATHER_SETTLED : GATHER_MOVING;
    for (int k = 0; k < steps && dt > 0; k++) {
        double tk = startT + (k + 0.5) * dt;
        // A migrating band is never content: full firelight extension.
        float wh = daylight::workHours(lat, tk, 1.0f);
        float flow = flowBase *
                     atmosphere::forageFactor(atmosphere::seasonalTempC(clim, n, h, tk));
        float H = std::min(flow, gather * b.P * wh / 12.0f);
        float cap = CAP_DAYS_BAND * std::max(b.P, 1.0f);
        float fill = std::clamp(b.S / cap, 0.0f, 1.0f);
        float excl = std::clamp(1.0f - fill / HOARD_FILL, 0.0f, 1.0f);
        float shortfall = b.P > 0 ? std::clamp(1.0f - H / b.P, 0.0f, 1.0f) : 0.0f;
        double a = startT + k * (double)dt;
        float act =
            dt >= 1.0f ? 1.0f : (float)(daylight::activeDays(lon, a, a + dt, wh) / dt);
        bandStarve(b.pop, STARVE_MAX * b.P * excl * shortfall * dt);
        b.P = b.pop.total();
        b.S = std::clamp(b.S + (H - b.P) * dt * act, 0.0f, CAP_DAYS_BAND * std::max(b.P, 1.0f));
    }
}

// What a group can bring to a fight: people, armed by their bows. Archery
// is dual-use, so the bow-making labour is a real choice between hunting
// better and being harder to rob.
// People weighted by who they are, then armed by their bows. The rest of
// a settlement's people are why it is harder to rob than a raiding party
// of the same headcount is to beat -- so there is no separate defender
// bonus any more; the advantage is the population itself.
inline float fightStrength(const population::Cohorts& c, float bows, float archExp,
                           float fightAff = 0) {
    using namespace population;
    float able = cohortStrength(c);
    float cover = bowCoverage(bows, std::max(c.M, 1.0f)); // bows are carried by the men
    return able * (FIGHT_UNARMED + (1.0f - FIGHT_UNARMED) * cover * archExp) *
           affinityBonus(fightAff);
}

// The raid: reached them, now settle it. Heavily chanced -- a party twice
// the strength wins about seven times in ten, not always -- and cheap in
// lives, because people run rather than fight to the end. What is taken is
// limited by what can be carried, except livestock, which walks itself.
inline void resolveRaid(population::Field& pf, technology::WorldState& ws,
                        population::Band& b, int ti, double now) {
    using namespace population;
    Settlement& t = pf.settlements[ti];
    float aExp = technology::expertise(b.tech[TECH_ARCHERY], now);
    float dExp = technology::expertise(t.tech[TECH_ARCHERY], now);
    float A = fightStrength(b.pop, b.bows, aExp, b.aff.fight) * RAID_INITIATIVE;
    float D = fightStrength(t.pop, t.bows, dExp, t.aff.fight);
    double pa = std::pow(std::max(A, 1e-3f), RAID_ODDS_POWER);
    double pd = std::pow(std::max(D, 1e-3f), RAID_ODDS_POWER);
    bool won = technology::urand(ws.rng) < pa / (pa + pd);
    float lossA = won ? RAID_LOSS_WINNER : RAID_LOSS_LOSER;
    float lossD = won ? RAID_LOSS_LOSER : RAID_LOSS_WINNER;
    // The men are the ones in the fight; a few of the rest are caught up
    // in it. A settlement that loses its men is crippled for a generation,
    // and the flows above are what let that scar heal slowly.
    b.pop.M *= 1.0f - lossA;
    b.P = b.pop.total();
    t.pop.M *= 1.0f - lossD;
    t.pop.C *= 1.0f - lossD * 0.25f;
    t.pop.W *= 1.0f - lossD * 0.25f;
    t.pop.E *= 1.0f - lossD * 0.25f;
    t.P = t.pop.total();
    b.bows *= 1.0f - lossA;
    t.bows *= 1.0f - lossD;
    if (won) {
        float carry = b.P * LOOT_CARRY_DAYS;
        b.loot = std::min(t.S * LOOT_STORE_SHARE, carry);
        b.lootHerd = t.herd * LOOT_HERD_SHARE;
        t.S = std::max(t.S - b.loot, 0.0f);
        t.herd = std::max(t.herd - b.lootHerd, 0.0f);
    }
    // Both sides learn the trade, whichever way it went. Being raided
    // makes a people dangerous, not merely poorer.
    b.aff.fight += (1.0f - b.aff.fight) * FIGHT_LEARN;
    t.aff.fight += (1.0f - t.aff.fight) * FIGHT_LEARN;
    {
        char txt[96];
        if (won)
            snprintf(txt, sizeof txt, "%s raided %s: %d rations, %d livestock taken", b.name,
                     t.name, (int)b.loot, (int)b.lootHerd);
        else
            snprintf(txt, sizeof txt, "%s beat off a raid by %s", t.name, b.name);
        note(pf, won ? population::EV_RAID_HIT : population::EV_RAID_HELD, now, t.id, b.homeId,
             b.id, won ? b.loot : 0.0f, txt);
    }
    b.returning = true;
    fprintf(stderr, "raid: %s settlement %u, %d rations %d livestock, day %.0f\n",
            won ? "sacked" : "beaten off by", t.id, (int)b.loot, (int)b.lootHerd, now);
}

// A band that arrives (or gives up) becomes a settlement on unclaimed ground.
inline void foundSettlement(population::Field& pf, technology::WorldState& ws,
                            const hydrology::Result& hy, const atmosphere::Climatology& clim,
                            const population::Band& b, int cell, double now) {
    using namespace population;
    Settlement s{cell, 0, false, b.pop, b.P, cellCondition(pf, cell, now), now, now};
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
    s.kFoodP = pf.kFoodPMap[cell];
    s.kGame = pf.kGameMap[cell];
    s.kSmall = pf.kSmallMap[cell];
    s.bows = b.bows; // carried on the march
    s.gRegion = population::gameRegion(cell);
    s.gameNow = pf.gameG.empty() ? 1.0f : pf.gameG[s.gRegion];
    s.kWater = pf.kWaterMap[cell];
    s.sFarm = pf.sFarmMap[cell];
    s.pasture = pf.pastureMap[cell];
    s.buildMat = pf.buildMatMap[cell];
    s.cycleT = now; // the fill cycle starts with the settlement
    s.S = std::min(b.S, CAP_DAYS_SETTLED * b.P);
    for (int t = 0; t < NTECH; t++) s.tech[t] = b.tech[t];
    if (s.tech[TECH_HUSBANDRY].practising) {
        // Herders arrive with stock driven along the march, not a bare
        // seed: enough to feed a share of the arrivals while it grows.
        float hExp = technology::expertise(s.tech[TECH_HUSBANDRY], now);
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
        if (distKm(n, cellCentre(pf.settlements[j].cell)) <= CONTACT_KM) {
            pf.neighbours[idx].push_back(j);
            pf.neighbours[j].push_back(idx);
        }
    for (int t = 0; t < NTECH; t++) {
        technology::redraw(pf, idx, ws, t, now);
        for (int j : pf.neighbours[idx]) technology::redraw(pf, j, ws, t, now);
        technology::scheduleInvention(pf, ws, t, now);
    }
    {
        char txt[96];
        int km = b.fromCell >= 0
                     ? (int)distKm(cellCentre(b.fromCell), cellCentre(cell))
                     : 0;
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
inline bool mergeBand(population::Field& pf, technology::WorldState& ws,
                      const population::Band& b, double now) {
    using namespace population;
    terrain::V3 n = {b.px, b.py, b.pz};
    int ti = -1;
    float td = CONTACT_KM;
    for (int i = 0; i < (int)pf.settlements.size(); i++) {
        if (pf.settlements[i].leaving) continue; // that place is being abandoned
        float d = distKm(n, cellCentre(pf.settlements[i].cell));
        if (d < td) { td = d; ti = i; }
    }
    if (ti < 0) return false;
    Settlement& t = pf.settlements[ti];
    t.pop.add(b.pop);
    t.P = t.pop.total();
    t.bows += b.bows;
    t.S = std::min(t.S + b.S, CAP_DAYS_SETTLED * t.P);
    for (int tc = 0; tc < NTECH; tc++) {
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
            if (tc == TECH_HUSBANDRY && t.herd <= 0) t.herd = technology::HERD_SEED;
            if (tc == TECH_FARMING) technology::redraw(pf, ti, ws, TECH_GRANARY, now);
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

// One band re-evaluation: integrate, move, then decide — rest, settle here,
// arrive, re-target, or give up. Returns false if the band no longer exists.
inline bool stepBand(population::Field& pf, technology::WorldState& ws,
                     const hydrology::Result& hy, const atmosphere::Climatology& clim, int bi,
                     double now) {
    using namespace population;
    Band& b = pf.bands[bi];
    terrain::V3 pos = {b.px, b.py, b.pz};
    double span = now - b.t;
    b.t = now;
    int hereCell = cellOf(pos);
    double startT = b.t - span; // b.t was already moved to now
    // 0 on water: crossings cost stores. The game-borne share of the cell's
    // yield follows the regional pool's health, like a settlement's does.
    float flowBase = pf.K[hereCell] * SUSTAIN_R;
    if (flowBase > 0 && pf.kFoodPMap[hereCell] > 0 && !pf.gameG.empty()) {
        float g = pf.gameG[population::gameRegion(hereCell)];
        float scale = (pf.kFoodPMap[hereCell] - pf.kGameMap[hereCell] * (1.0f - huntEff(g))) /
                      pf.kFoodPMap[hereCell];
        flowBase *= std::max(scale, 0.0f);
    }
    integrateBand(b, flowBase, span, b.resting, clim, pos,
                  std::max(hy.heightM[hereCell], 0.0f), startT);
    if (b.P < BAND_MIN_P) {
        if (!mergeBand(pf, ws, b, now)) logAt("perished", bi, pos, b.P, now);
        pf.bands.erase(pf.bands.begin() + bi);
        return false;
    }
    // A raiding party has somewhere to be and does not settle en route:
    // outward to its mark, then home with whatever it took.
    if (b.purpose == BAND_RAID) {
        int hi = indexById(pf, b.homeId);
        if (!b.returning) {
            int ti = indexById(pf, b.targetId);
            if (ti < 0) b.returning = true; // they moved on; nothing to rob
            else {
                b.targetCell = pf.settlements[ti].cell;
                if (distKm(pos, cellCentre(b.targetCell)) < 20.0f)
                    resolveRaid(pf, ws, b, ti, now);
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
                Settlement& h = pf.settlements[hi];
                h.pop.add(b.pop); // the survivors, back among their people
                h.aff.fight = std::max(h.aff.fight, b.aff.fight);
                h.P = h.pop.total();
                h.bows += b.bows;
                h.herd += b.lootHerd;
                h.S = std::min(h.S + b.S + b.loot, storageCapDays(h.P, h.granaries) * h.P);
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
        float km = BAND_SPEED_KM_DAY / 12.0f * lh *
                   (float)daylight::activeDays(lonB, now - span, now, lh);
        float factor = 1.0f;
        bool water = hy.heightM[hereCell] <= 0 ||
                     hy.cells[hereCell].lakeLevel > hydrology::NO_LAKE + 1;
        float hHere = std::max(hy.heightM[hereCell], 0.0f);
        bool frozen = seasonalT(clim, pos, hHere, now) < FROZEN_T;
        if (water && !frozen) factor *= RAFT_FACTOR;
        else if (!water && !frozen && hy.cells[hereCell].flow >= RIVER_MAJOR_KM2)
            factor *= RIVER_CROSS_FACTOR;
        pos = moveToward(pos, tgt, km * factor);
        b.px = pos.x;
        b.py = pos.y;
        b.pz = pos.z;
    }
    int cell = cellOf(pos);
    if (b.purpose == BAND_RAID) { // no resting, no founding: keep marching
        b.nextUpdate = now + BAND_STEP_DAYS;
        return true;
    }
    float fill = b.S / (CAP_DAYS_BAND * std::max(b.P, 1.0f));
    if (b.resting) {
        if (fill >= 0.95f || now - b.restStart > 90.0) b.resting = false;
    } else if (fill < 0.3f && pf.K[cell] * SUSTAIN_R > b.P) {
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
    float fExp = technology::expertise(b.tech[TECH_FARMING], now);
    float hExp = technology::expertise(b.tech[TECH_HUSBANDRY], now);
    auto canHold = [&](int c) { return moverCap(pf, c, fExp, hExp, now) >= b.P; };
    if (distKm(pos, tgt) < 20.0f) {
        // Arrived: the rumour meets reality.
        if (pf.settlementAt[cell] < 0 && moverCap(pf, cell, fExp, hExp, now) >= MIN_SETTLEMENT_K &&
            canHold(cell) && spacingOK(pf, pos)) {
            foundSettlement(pf, ws, hy, clim, b, cell, now);
            done = true;
        } else {
            double rest = b.resting ? now - b.restStart : 0.0;
            int nt = bestProspect(pf, pos, ws.rng,
                                  bandAwareKm(rest, prominenceM(hy, clim, cell)), now, fExp, hExp);
            if (nt >= 0) b.targetCell = nt;
            else {
                if (!mergeBand(pf, ws, b, now)) logAt("perished", bi, pos, b.P, now);
                done = true;
            }
        }
    } else if (!b.resting && pf.settlementAt[cell] < 0 &&
               moverCap(pf, cell, fExp, hExp, now) >= MIN_SETTLEMENT_K && canHold(cell) &&
               moverCap(pf, cell, fExp, hExp, now) >=
                   hopeRatio(now - b.setOut) * moverCap(pf, b.targetCell, fExp, hExp, now) &&
               spacingOK(pf, pos)) {
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
    b.nextUpdate = now + BAND_STEP_DAYS;
    return true;
}

// Sustained scarcity with enough people: a third leaves as a band, taking the
// settlement's knowledge and technology with it.
// Who is worth robbing: the richest neighbour we could plausibly beat.
// Only settlements within contact range are candidates -- you rob the
// people you know about -- and livestock counts double, being wealth that
// carries itself home.
inline int raidTarget(const population::Field& pf, int si, double now, float strength) {
    using namespace population;
    const Settlement& s = pf.settlements[si];
    int best = -1;
    float bestScore = 0;
    for (int j : pf.neighbours[si]) {
        const Settlement& t = pf.settlements[j];
        if (t.leaving || t.P < BAND_MIN_P) continue;
        float prize = t.S * LOOT_STORE_SHARE + t.herd * LOOT_HERD_SHARE * LOOT_CARRY_DAYS;
        if (prize < RAID_WORTH_IT) continue; // not worth the walk
        float dExp = technology::expertise(t.tech[TECH_ARCHERY], now);
        float def = fightStrength(t.pop, t.bows, dExp, t.aff.fight);
        float score = prize / std::max(def, 1.0f);
        if (score > bestScore) { bestScore = score; best = j; }
    }
    // You rob people you can beat. With non-combatants counted at their
    // real weight, a settlement defends far better than its headcount
    // suggests, so this rejects most neighbours outright.
    if (best >= 0) {
        const Settlement& t = pf.settlements[best];
        float dExp = technology::expertise(t.tech[TECH_ARCHERY], now);
        if (fightStrength(t.pop, t.bows, dExp, t.aff.fight) > strength) return -1;
    }
    return best;
}

// Nowhere to go, and hungry: the third answer. Sends a party to rob the
// best neighbour within reach. Returns true if one set out.
inline bool maybeRaid(population::Field& pf, technology::WorldState& ws, int si, double now) {
    using namespace population;
    Settlement& s = pf.settlements[si];
    if (s.pop.M * RAID_MEN_SHARE < RAID_MIN_P) return false;
    float archExp = technology::expertise(s.tech[TECH_ARCHERY], now);
    Cohorts party{};
    party.M = s.pop.M * RAID_MEN_SHARE; // the men go; the rest stay
    float strength =
        fightStrength(party, s.bows * RAID_MEN_SHARE, archExp, s.aff.fight) * RAID_INITIATIVE;
    int ti = raidTarget(pf, si, now, strength);
    if (ti < 0) return false;
    terrain::V3 home = cellCentre(s.cell);
    Band b{};
    b.id = pf.nextBandId++;
    b.purpose = BAND_RAID;
    b.homeId = s.id;
    b.targetId = pf.settlements[ti].id;
    b.px = home.x;
    b.py = home.y;
    b.pz = home.z;
    b.culture = s.culture;
    b.aff = s.aff;
    for (int i = 0; i < 16; i++) b.name[i] = s.name[i];
    b.pop = party;
    b.P = party.total();
    b.bows = s.bows * RAID_MEN_SHARE;
    b.S = std::min(s.S * RAID_MEN_SHARE, CAP_DAYS_BAND * b.P);
    b.targetCell = pf.settlements[ti].cell;
    b.t = now;
    b.nextUpdate = now + BAND_STEP_DAYS;
    for (int t = 0; t < NTECH; t++) b.tech[t] = s.tech[t];
    s.pop.M -= party.M;
    s.P = s.pop.total();
    s.S -= b.S;
    s.bows -= b.bows;
    pf.bands.push_back(b);
    {
        char txt[96];
        snprintf(txt, sizeof txt, "%s sent %d warriors against %s", s.name, (int)b.P,
                 pf.settlements[ti].name);
        note(pf, EV_RAID_LAUNCH, now, s.id, pf.settlements[ti].id, b.id, b.P, txt);
    }
    logAt("raiding party leaves settlement", si, home, b.P, now);
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
                                 const hydrology::Result& hy,
                                 const atmosphere::Climatology& clim, int si, double now) {
    using namespace population;
    Settlement& s = pf.settlements[si];
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
    bool bleeding = s.starvedYr > STARVE_NOTICE * std::max(s.P, 1.0f);
    bool foodLimited = phi < PHI_CONTENT || bleeding;
    // Sustained hunger for need-driven invention: a genuine shortfall
    // (phi < NEED_HUNGRY_PHI), not the comfort glide. Checked before every
    // early return so small settlements get desperate too; leaving or
    // splitting does not reset it -- neither cures desperation by itself.
    if (phi >= NEED_HUNGRY_PHI && !bleeding) s.hungrySince = -1;
    else if (s.hungrySince < 0) s.hungrySince = now;
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
    bool starving = phi < NEED_HUNGRY_PHI || bleeding;
    if (s.scarceSince < 0) {
        s.scarceSince = now;
        if (starving) s.nextUpdate = std::min(s.nextUpdate, now + SPLIT_AFTER_DAYS);
        return;
    }
    // Never schedule into the past: a deadline that has already gone by
    // would be re-popped from the queue forever (the event time would keep
    // matching), rewinding the settlement's clock instead of advancing it.
    // Real hunger restores the short cadence: things got worse, so they
    // look again in earnest.
    if (phi < NEED_HUNGRY_PHI || bleeding) s.lookAgainDays = SPLIT_AFTER_DAYS;
    if (starving)
        s.nextUpdate =
            std::min(s.nextUpdate, std::max(s.scarceSince + s.lookAgainDays, now + 5.0));
    if (now - s.scarceSince < s.lookAgainDays) return;
    s.scarceSince = now; // whether or not anyone leaves, the pressure resets
    if (starving) s.nextUpdate = std::min(s.nextUpdate, now + s.lookAgainDays);
    if (s.P < BAND_MIN_P) return; // too few to survive any journey
    float fExp = technology::expertise(s.tech[TECH_FARMING], now);
    float hExp = technology::expertise(s.tech[TECH_HUSBANDRY], now);
    terrain::V3 home = cellCentre(s.cell);
    float est = 0;
    int tgt = bestProspect(pf, home, ws.rng,
                           settlementAwareKm(now - s.founded, prominenceM(hy, clim, s.cell)), now,
                           fExp, hExp, &est);
    bool wasStuck = s.noProspect; // the last survey came up empty too
    s.noProspect = tgt < 0;
    if (tgt < 0) {
        // Circumscription: hemmed in, hungry, and nowhere to go. This is
        // where raiding comes from -- but only once it is clear there is no
        // land to be had, not on the first disappointing look around.
        bool raided = wasStuck && maybeRaid(pf, ws, si, now);
        s.lookAgainDays = std::min(s.lookAgainDays * 2.0, LOOK_BACKOFF_MAX);
        (void)raided;
        return;
    }

    // Judged on the rumour, not the truth: a group deciding whether to pick
    // up and leave knows only what it has heard, and distant ground is
    // reported optimistically as often as not. Arriving to a poorer valley
    // than promised is a real outcome -- the band re-evaluates on arrival
    // against this same measure, so nobody marches toward ground they would
    // refuse when they got there.
    float targetSupport = est * SUSTAIN_R;
    float homeSupport = keff * s.R; // what this place carries in its present state
    float anchor = 1.0f + RELOC_ANCHOR_GRANARY * s.granaries + RELOC_ANCHOR_FARM * fExp;
    bool wholeGroup = targetSupport >= s.P && targetSupport >= anchor * homeSupport;
    if (!wholeGroup && s.P < SPLIT_MIN_P) { // nowhere for all, too few to divide: endure
        s.lookAgainDays = std::min(s.lookAgainDays * 2.0, LOOK_BACKOFF_MAX);
        return;
    }
    s.lookAgainDays = SPLIT_AFTER_DAYS; // something came of it: keep looking

    Band b{};
    b.id = pf.nextBandId++;
    b.px = home.x;
    b.py = home.y;
    b.pz = home.z;
    b.culture = s.culture;
    b.aff = s.aff;
    b.colonists = !wholeGroup; // a splinter, not the town on the move
    for (int i = 0; i < 16; i++) b.name[i] = s.name[i];
    b.pop = s.pop;
    if (!wholeGroup) b.pop.scale(SPLIT_SHARE);
    b.P = b.pop.total();
    b.S = std::min((wholeGroup ? s.S : s.S * SPLIT_SHARE), CAP_DAYS_BAND * b.P);
    b.bows = wholeGroup ? s.bows : s.bows * SPLIT_SHARE; // people take their bows
    b.targetCell = tgt;
    b.setOut = now;
    b.fromCell = s.cell;
    b.t = now;
    b.nextUpdate = now + BAND_STEP_DAYS;
    for (int t = 0; t < NTECH; t++) b.tech[t] = s.tech[t];
    if (wholeGroup) {
        b.sid = s.id; // they are still themselves, wherever they end up
        // The land remembers what it was left in; only a place that was
        // invested in leaves anything to find.
        markScar(pf, s.cell, s.R, now, s.id);
        if (s.granaries >= 1.0f || now - s.founded > RUIN_MIN_AGE_DAYS) {
            Field::Ruin r{s.cell, now, {}};
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
        s.pop = Cohorts{};
        s.P = 0;
        s.S = 0;
        s.herd = 0;
        s.nextUpdate = 1e18;
        for (int t = 0; t < NTECH; t++) s.nextTech[t] = 1e18;
        {
            char txt[96];
            snprintf(txt, sizeof txt, "%s abandoned their home and set out", s.name);
            note(pf, EV_RELOCATE, now, s.id, 0, b.id, b.P, txt);
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
            note(pf, EV_SPLIT, now, s.id, 0, b.id, b.P, txt);
        }
        logAt("split from settlement", si, home, b.P, now);
    }
    pf.bands.push_back(b);
}

// Erase the settlements that walked away this step and weather old ruins.
// Runs once, after the event loop, so indices stay valid while events are
// being processed; panels track settlements by id, not index.
inline void sweepDeparted(population::Field& pf, double now) {
    using namespace population;
    for (int i = (int)pf.ruins.size() - 1; i >= 0; i--)
        if (now - pf.ruins[i].abandoned > RUIN_LIFE_DAYS) pf.ruins.erase(pf.ruins.begin() + i);
    int n = (int)pf.settlements.size();
    bool any = false;
    for (int i = 0; i < n && !any; i++) any = pf.settlements[i].leaving;
    if (!any) return;
    std::vector<int> nu(n, -1);
    int k = 0;
    for (int i = 0; i < n; i++)
        if (!pf.settlements[i].leaving) nu[i] = k++;
    for (int i = 0; i < n; i++) {
        int cell = pf.settlements[i].cell;
        if (pf.settlementAt[cell] == i) pf.settlementAt[cell] = nu[i]; // -1 frees the site
    }
    std::vector<std::vector<int>> nb(k);
    for (int i = 0; i < n; i++) {
        if (nu[i] < 0) continue;
        for (int j : pf.neighbours[i])
            if (nu[j] >= 0) nb[nu[i]].push_back(nu[j]);
    }
    std::vector<Settlement> keep;
    keep.reserve(k);
    for (int i = 0; i < n; i++)
        if (nu[i] >= 0) keep.push_back(pf.settlements[i]);
    pf.settlements.swap(keep);
    pf.neighbours.swap(nb);
}

// Process every due event in chronological order: settlement re-evaluations
// (with split checks), contact draws, the world invention clock, and band
// steps. Order matters because each event changes the rates around it.
// Process every due event in chronological order through a lazy priority
// queue: entries are (time, kind, id); each pop is validated against the
// authoritative next-time and stale entries are skipped. Kinds: 0 world
// invention clock (id = tech), 1 settlement wake, 2 settlement contact draw
// (idx, tech), 3 band step (id = band id, stable across erases). After the
// loop, a catch-up pass brings every settlement and band current to `now` --
// any step size leaves the whole world exact at the displayed moment.
inline bool simulate(population::Field& pf, technology::WorldState& ws,
                     const hydrology::Result& hy, const atmosphere::Climatology& clim, double now) {
    using namespace population;
    if (pf.settlements.empty()) return false;
    bool changed = false;

    struct Ev {
        double t;
        int kind, idx, tech;
        bool operator<(const Ev& o) const { return t > o.t; } // min-heap
    };
    std::priority_queue<Ev> q;

    struct HeapSink : technology::WorldState::Sink {
        std::priority_queue<Ev>* q;
        void techEvent(int idx, int tech, double when) override {
            if (when < 1e17) q->push({when, 2, idx, tech});
        }
        void clockEvent(int tech, double when) override {
            if (when < 1e17) q->push({when, 0, 0, tech});
        }
    } sink;
    sink.q = &q;
    ws.sink = &sink;

    auto pushSettlement = [&](int i) {
        const Settlement& s = pf.settlements[i];
        if (s.nextUpdate < 1e17) q.push({s.nextUpdate, 1, i, 0});
        for (int t = 0; t < NTECH; t++)
            if (s.nextTech[t] < 1e17) q.push({s.nextTech[t], 2, i, t});
    };
    auto pushBand = [&](const Band& b) { q.push({b.nextUpdate, 3, (int)b.id, 0}); };
    for (int i = 0; i < (int)pf.settlements.size(); i++) pushSettlement(i);
    for (const Band& b : pf.bands) pushBand(b);
    for (int t = 0; t < NTECH; t++)
        if (ws.nextEvent[t] < 1e17) q.push({ws.nextEvent[t], 0, 0, t});
    if (!pf.gameG.empty()) q.push({pf.gameT + GAME_TICK_DAYS, 4, 0, 0});

    while (!q.empty() && q.top().t <= now) {
        pf.peakBands = std::max(pf.peakBands, pf.bands.size()); // high-water mark
        Ev ev = q.top();
        q.pop();
        double t = ev.t;
        if (ev.kind == 0) {
            if (t != ws.nextEvent[ev.tech]) continue; // stale
            if (!ws.fires[ev.tech]) { technology::scheduleInvention(pf, ws, ev.tech, t); continue; }
            int wi = technology::pickInventor(pf, ws, ev.tech, t);
            if (wi >= 0) {
                technology::startPractising(pf, wi, ws, ev.tech, t);
                {
                    char txt[96];
                    snprintf(txt, sizeof txt, "%s invented %s!", pf.settlements[wi].name,
                             technology::techName(ev.tech));
                    note(pf, EV_INVENTED, t, pf.settlements[wi].id, 0, 0, (float)ev.tech, txt);
                }
                fprintf(stderr, "tech: %s invented at settlement %d, day %.0f\n",
                        technology::techName(ev.tech), wi, t);
                changed = true;
            }
            technology::scheduleInvention(pf, ws, ev.tech, t);
        } else if (ev.kind == 1) {
            Settlement& s = pf.settlements[ev.idx];
            if (t != s.nextUpdate) continue;
            changed |= population::advance(s, technology::effectiveK(s, t),
                                           seasonCtx(s, hy, clim, t), t);
            reportGranaries(pf, s, t);
            size_t bandsBefore = pf.bands.size();
            // Decide first: leaving or growing scarce can pull the next wake
            // earlier, and the queue entry must carry the final time.
            maybeRelocateOrSplit(pf, ws, hy, clim, ev.idx, t);
            if (s.nextUpdate < 1e17) q.push({s.nextUpdate, 1, ev.idx, 0});
            for (size_t b = bandsBefore; b < pf.bands.size(); b++) pushBand(pf.bands[b]);
        } else if (ev.kind == 2) {
            Settlement& s = pf.settlements[ev.idx];
            if (t != s.nextTech[ev.tech]) continue;
            if (!s.techFires[ev.tech]) { technology::redraw(pf, ev.idx, ws, ev.tech, t); continue; }
            if (!s.tech[ev.tech].aware) {
                s.tech[ev.tech].aware = true;
                fprintf(stderr, "tech: settlement %d aware of %s, day %.0f\n", ev.idx,
                        technology::techName(ev.tech), t);
                technology::redraw(pf, ev.idx, ws, ev.tech, t);
                for (int j : pf.neighbours[ev.idx])
                    if (!pf.settlements[j].tech[ev.tech].aware)
                        technology::redraw(pf, j, ws, ev.tech, t);
            } else {
                technology::startPractising(pf, ev.idx, ws, ev.tech, t);
                {
                    char txt[96];
                    snprintf(txt, sizeof txt, "%s took up %s", s.name,
                             technology::techName(ev.tech));
                    note(pf, EV_ADOPTED, t, s.id, 0, 0, (float)ev.tech, txt);
                }
                fprintf(stderr, "tech: settlement %d starts %s, day %.0f\n", ev.idx,
                        technology::techName(ev.tech), t);
            }
            technology::scheduleInvention(pf, ws, ev.tech, t);
            changed = true;
        } else if (ev.kind == 4) {
            if (std::fabs(t - (pf.gameT + GAME_TICK_DAYS)) > 1e-6) continue; // stale
            gameTick(pf, t);
            q.push({pf.gameT + GAME_TICK_DAYS, 4, 0, 0});
        } else {
            int bi = -1;
            for (int i = 0; i < (int)pf.bands.size(); i++)
                if ((int)pf.bands[i].id == ev.idx) { bi = i; break; }
            if (bi < 0 || t != pf.bands[bi].nextUpdate) continue;
            size_t settsBefore = pf.settlements.size();
            bool alive = stepBand(pf, ws, hy, clim, bi, t);
            if (alive) pushBand(pf.bands[bi]);
            for (size_t i = settsBefore; i < pf.settlements.size(); i++) pushSettlement((int)i);
            changed = true;
        }
    }
    ws.sink = nullptr;

    // Catch-up: bring every settlement and band current to `now`, whatever
    // the step size -- minute steps show the world in full detail, big steps
    // aggregate through the event loop above first (Design/Event-Driven).
    for (int i = 0; i < (int)pf.settlements.size(); i++) {
        Settlement& s = pf.settlements[i];
        if (!s.leaving && s.t < now - 1e-9) {
            changed |= population::advance(s, technology::effectiveK(s, now),
                                           seasonCtx(s, hy, clim, now), now);
            reportGranaries(pf, s, now);
            maybeRelocateOrSplit(pf, ws, hy, clim, i, now);
        }
    }
    for (int i = (int)pf.bands.size() - 1; i >= 0; i--)
        if (pf.bands[i].t < now - 1e-9) {
            stepBand(pf, ws, hy, clim, i, now);
            changed = true;
        }
    pf.peakBands = std::max(pf.peakBands, pf.bands.size());
    if (pf.gameT < now - 1e-9) gameTick(pf, now);
    sweepDeparted(pf, now); // settlements that left are erased, not tombstoned
    return changed;
}

} // namespace sim
