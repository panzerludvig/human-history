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

inline float distKm(terrain::V3 a, terrain::V3 b) {
    return std::acos(std::clamp(terrain::dot(a, b), -1.0f, 1.0f)) * 6371.0f;
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
inline float moverCap(const population::Field& pf, int cell, float farmExp, float husbExp) {
    using namespace population;
    float food = pf.kFoodPMap[cell];
    if (food <= 0) return 0;
    food += food * technology::FARM_YIELD_GAIN * pf.sFarmMap[cell] * farmExp;
    if (husbExp > 0)
        food += pf.pastureMap[cell] * FORAGE_KM2 * HERD_PASTURE_K / SUSTAIN_R *
                (0.3f + 0.7f * husbExp) * 0.85f;
    return std::min(food, pf.kWaterMap[cell]);
}

// The best-looking unclaimed prospect within the knowledge range, judged with
// noise that grows with distance: near things resolve exactly, far things are
// rumours; each candidate is valued at what THIS mover could make of it.
// Returns a cell index, or -1 if nothing known is worth going to.
inline int bestProspect(const population::Field& pf, terrain::V3 from, uint64_t& rng,
                        float radiusKm, float farmExp = 0, float husbExp = 0) {
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
            float cap = pf.K[cell];
            if (skilled && pf.kFoodPMap[cell] > 0) cap = moverCap(pf, cell, farmExp, husbExp);
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
        if (spacingOK(pf, cellCentre(cell))) return cell;
        top[bi] = top[--nTop];
    }
    return -1;
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
        float gameFlow = s.kGame * huntEff(g) * s.meanF;
        float farmMult = 1.0f + technology::FARM_YIELD_GAIN * s.sFarm *
                                    technology::expertise(s.tech[TECH_FARMING], now);
        float hExp = technology::expertise(s.tech[TECH_HUSBANDRY], now);
        float total = (s.kFoodP - s.kGame) * s.meanF + gameFlow +
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
        pf.gameG[r] = std::clamp(g + (regen - depl) * (float)dt, 0.0f, 1.0f);
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
        b.P = std::max(b.P - STARVE_MAX * b.P * excl * shortfall * dt, 0.0f);
        b.S = std::clamp(b.S + (H - b.P) * dt * act, 0.0f, CAP_DAYS_BAND * std::max(b.P, 1.0f));
    }
}

// A band that arrives (or gives up) becomes a settlement on unclaimed ground.
inline void foundSettlement(population::Field& pf, technology::WorldState& ws,
                            const hydrology::Result& hy, const atmosphere::Climatology& clim,
                            const population::Band& b, int cell, double now) {
    using namespace population;
    Settlement s{cell, b.P, 1.0f, now, now};
    s.founded = now;
    s.kFoodP = pf.kFoodPMap[cell];
    s.kGame = pf.kGameMap[cell];
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
        float d = distKm(n, cellCentre(pf.settlements[i].cell));
        if (d < td) { td = d; ti = i; }
    }
    if (ti < 0) return false;
    Settlement& t = pf.settlements[ti];
    t.P += b.P;
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
    float fill = b.S / (CAP_DAYS_BAND * std::max(b.P, 1.0f));
    if (b.resting) {
        if (fill >= 0.95f || now - b.restStart > 90.0) b.resting = false;
    } else if (fill < 0.3f && pf.K[cell] * SUSTAIN_R > b.P) {
        b.resting = true;
        b.restStart = now;
    }
    bool done = false;
    // The band's skills decide what ground is worth settling (moverCap):
    // herders take steppe a forager would starve on.
    float fExp = technology::expertise(b.tech[TECH_FARMING], now);
    float hExp = technology::expertise(b.tech[TECH_HUSBANDRY], now);
    if (distKm(pos, tgt) < 20.0f) {
        // Arrived: the rumour meets reality.
        if (pf.settlementAt[cell] < 0 && moverCap(pf, cell, fExp, hExp) >= MIN_SETTLEMENT_K &&
            spacingOK(pf, pos) && (int)pf.settlements.size() < MAX_TOTAL_SETTLEMENTS) {
            foundSettlement(pf, ws, hy, clim, b, cell, now);
            done = true;
        } else {
            double rest = b.resting ? now - b.restStart : 0.0;
            int nt = bestProspect(pf, pos, ws.rng,
                                  bandAwareKm(rest, prominenceM(hy, clim, cell)), fExp, hExp);
            if (nt >= 0) b.targetCell = nt;
            else {
                if (!mergeBand(pf, ws, b, now)) logAt("perished", bi, pos, b.P, now);
                done = true;
            }
        }
    } else if (!b.resting && pf.settlementAt[cell] < 0 &&
               moverCap(pf, cell, fExp, hExp) >= MIN_SETTLEMENT_K &&
               moverCap(pf, cell, fExp, hExp) >=
                   0.9f * moverCap(pf, b.targetCell, fExp, hExp) &&
               spacingOK(pf, pos) && (int)pf.settlements.size() < MAX_TOTAL_SETTLEMENTS) {
        // Good enough ground under their feet beats a distant rumour.
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
inline void maybeSplit(population::Field& pf, technology::WorldState& ws,
                       const hydrology::Result& hy, const atmosphere::Climatology& clim, int si,
                       double now) {
    using namespace population;
    Settlement& s = pf.settlements[si];
    float keff = technology::effectiveK(s, now);
    float phi = s.P > 1 ? keff * s.R / s.P : 2.0f;
    // Sustained hunger for need-driven invention: a genuine shortfall
    // (phi < NEED_HUNGRY_PHI), not the comfort glide. Checked before every
    // early return so small settlements get desperate too; split attempts
    // do not reset it -- emigration does not cure desperation.
    if (phi >= NEED_HUNGRY_PHI) s.hungrySince = -1;
    else if (s.hungrySince < 0) s.hungrySince = now;
    if (phi >= SPLIT_PHI) {
        s.scarceSince = -1;
        s.noProspect = false;
        return;
    }
    if (s.P < SPLIT_MIN_P) return;
    if (s.scarceSince < 0) {
        s.scarceSince = now;
        return;
    }
    if (now - s.scarceSince < SPLIT_AFTER_DAYS || (int)pf.bands.size() >= MAX_BANDS) return;
    terrain::V3 home = cellCentre(s.cell);
    int tgt = bestProspect(pf, home, ws.rng,
                           settlementAwareKm(now - s.founded, prominenceM(hy, clim, s.cell)),
                           technology::expertise(s.tech[TECH_FARMING], now),
                           technology::expertise(s.tech[TECH_HUSBANDRY], now));
    s.scarceSince = now; // whether or not anyone leaves, the pressure resets
    s.noProspect = tgt < 0;
    if (tgt < 0) return;
    Band b{};
    b.id = pf.nextBandId++;
    b.px = home.x;
    b.py = home.y;
    b.pz = home.z;
    b.P = s.P * SPLIT_SHARE;
    b.S = std::min(s.S * SPLIT_SHARE, CAP_DAYS_BAND * b.P);
    b.targetCell = tgt;
    b.t = now;
    b.nextUpdate = now + BAND_STEP_DAYS;
    for (int t = 0; t < NTECH; t++) b.tech[t] = s.tech[t];
    s.P -= b.P;
    s.S -= b.S;
    pf.bands.push_back(b);
    logAt("split from settlement", si, home, b.P, now);
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
        Ev ev = q.top();
        q.pop();
        double t = ev.t;
        if (ev.kind == 0) {
            if (t != ws.nextEvent[ev.tech]) continue; // stale
            if (!ws.fires[ev.tech]) { technology::scheduleInvention(pf, ws, ev.tech, t); continue; }
            int wi = technology::pickInventor(pf, ws, ev.tech, t);
            if (wi >= 0) {
                technology::startPractising(pf, wi, ws, ev.tech, t);
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
            if (s.nextUpdate < 1e17) q.push({s.nextUpdate, 1, ev.idx, 0});
            size_t bandsBefore = pf.bands.size();
            maybeSplit(pf, ws, hy, clim, ev.idx, t);
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
        if (s.t < now - 1e-9) {
            changed |= population::advance(s, technology::effectiveK(s, now),
                                           seasonCtx(s, hy, clim, now), now);
            maybeSplit(pf, ws, hy, clim, i, now);
        }
    }
    for (int i = (int)pf.bands.size() - 1; i >= 0; i--)
        if (pf.bands[i].t < now - 1e-9) {
            stepBand(pf, ws, hy, clim, i, now);
            changed = true;
        }
    if (pf.gameT < now - 1e-9) gameTick(pf, now);
    return changed;
}

} // namespace sim
