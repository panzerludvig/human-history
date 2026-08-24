// The simulation orchestrator: processes settlement re-evaluations, technology
// events, splits, and band journeys in strict chronological order, since each
// event can change the rates of the others. See Design/Migration.md.
#pragma once
#include "technology.h"
#include <cmath>
#include <cstdio>

namespace sim {

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

// The best-looking unclaimed prospect within the knowledge range, judged with
// noise that grows with distance: near things resolve exactly, far things are
// rumours. Returns a cell index, or -1 if nothing known is worth going to.
inline int bestProspect(const population::Field& pf, terrain::V3 from, uint64_t& rng) {
    using namespace population;
    float lat0 = std::asin(std::clamp(from.z, -1.0f, 1.0f));
    float dLat = KNOW_RADIUS_KM / 6371.0f;
    int y0 = std::max((int)(((lat0 - dLat) + 3.14159265f / 2) / 3.14159265f * H), 1);
    int y1 = std::min((int)(((lat0 + dLat) + 3.14159265f / 2) / 3.14159265f * H) + 1, H - 2);
    int best = -1;
    float bestScore = 0;
    for (int y = y0; y <= y1; y += 2)
        for (int x = 0; x < W; x += 2) {
            int cell = y * W + x;
            if (pf.K[cell] < MIN_SETTLEMENT_K) continue;
            terrain::V3 n = cellCentre(cell);
            float d = distKm(from, n);
            if (d > KNOW_RADIUS_KM || d < 80.0f) continue;
            if (!spacingOK(pf, n)) continue;
            float noise = ((float)technology::urand(rng) * 2.0f - 1.0f) * 0.6f * (d / KNOW_RADIUS_KM);
            float est = pf.K[cell] * (1.0f + noise);
            if (est > bestScore) { bestScore = est; best = cell; }
        }
    return best;
}

// A band forages the cell it stands on: same famine rule as a settlement, but
// a moving band gathers on a third of the day and carries only a small store.
// No growth on the march; a migration is months, not generations.
inline void integrateBand(population::Band& b, float flow, double span, bool resting) {
    using namespace population;
    int steps = std::clamp((int)(span / 2.0) + 1, 1, 60);
    float dt = (float)(span / steps);
    float gather = resting ? GATHER_SETTLED : GATHER_MOVING;
    for (int k = 0; k < steps && dt > 0; k++) {
        float H = std::min(flow, gather * b.P);
        float cap = CAP_DAYS_BAND * std::max(b.P, 1.0f);
        float fill = std::clamp(b.S / cap, 0.0f, 1.0f);
        float excl = std::clamp(1.0f - fill / HOARD_FILL, 0.0f, 1.0f);
        float shortfall = b.P > 0 ? std::clamp(1.0f - H / b.P, 0.0f, 1.0f) : 0.0f;
        b.P = std::max(b.P - STARVE_MAX * b.P * excl * shortfall * dt, 0.0f);
        b.S = std::clamp(b.S + (H - b.P) * dt, 0.0f, CAP_DAYS_BAND * std::max(b.P, 1.0f));
    }
}

// A band that arrives (or gives up) becomes a settlement on unclaimed ground.
inline void foundSettlement(population::Field& pf, technology::WorldState& ws,
                            const population::Band& b, int cell, double now) {
    using namespace population;
    Settlement s{cell, b.P, 1.0f, now, now};
    s.kFoodP = pf.kFoodPMap[cell];
    s.kWater = pf.kWaterMap[cell];
    s.sFarm = pf.sFarmMap[cell];
    s.S = std::min(b.S, CAP_DAYS_SETTLED * b.P);
    s.aware = b.aware;
    s.practising = b.practising;
    s.practiceT = b.practiceT;
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
    technology::redraw(pf, idx, ws, now);
    for (int j : pf.neighbours[idx]) technology::redraw(pf, j, ws, now);
    technology::scheduleInvention(pf, ws, now);
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
    if (b.aware && !t.aware) {
        t.aware = true;
        technology::redraw(pf, ti, ws, now);
        for (int j : pf.neighbours[ti])
            if (!pf.settlements[j].aware) technology::redraw(pf, j, ws, now);
        technology::scheduleInvention(pf, ws, now);
    }
    if (b.practising && !t.practising) {
        t.practising = true;
        t.practiceT = b.practiceT;
        t.nextTech = 1e18;
        for (int j : pf.neighbours[ti]) technology::redraw(pf, j, ws, now);
        technology::scheduleInvention(pf, ws, now);
    }
    logAt("merged into settlement", ti, n, b.P, now);
    return true;
}

// One band re-evaluation: integrate, move, then decide — rest, settle here,
// arrive, re-target, or give up. Returns false if the band no longer exists.
inline bool stepBand(population::Field& pf, technology::WorldState& ws, int bi, double now) {
    using namespace population;
    Band& b = pf.bands[bi];
    terrain::V3 pos = {b.px, b.py, b.pz};
    double span = now - b.t;
    b.t = now;
    float flow = pf.K[cellOf(pos)] * SUSTAIN_R; // 0 on water: crossings cost stores
    integrateBand(b, flow, span, b.resting);
    if (b.P < BAND_MIN_P) {
        if (!mergeBand(pf, ws, b, now)) logAt("perished", bi, pos, b.P, now);
        pf.bands.erase(pf.bands.begin() + bi);
        return false;
    }
    terrain::V3 tgt = cellCentre(b.targetCell);
    if (!b.resting) {
        pos = moveToward(pos, tgt, BAND_SPEED_KM_DAY * (float)span);
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
    if (distKm(pos, tgt) < 20.0f) {
        // Arrived: the rumour meets reality.
        if (pf.settlementAt[cell] < 0 && pf.K[cell] >= MIN_SETTLEMENT_K && spacingOK(pf, pos) &&
            (int)pf.settlements.size() < MAX_TOTAL_SETTLEMENTS) {
            foundSettlement(pf, ws, b, cell, now);
            done = true;
        } else {
            int nt = bestProspect(pf, pos, ws.rng);
            if (nt >= 0) b.targetCell = nt;
            else {
                if (!mergeBand(pf, ws, b, now)) logAt("perished", bi, pos, b.P, now);
                done = true;
            }
        }
    } else if (!b.resting && pf.settlementAt[cell] < 0 && pf.K[cell] >= MIN_SETTLEMENT_K &&
               pf.K[cell] >= 0.9f * pf.K[b.targetCell] && spacingOK(pf, pos) &&
               (int)pf.settlements.size() < MAX_TOTAL_SETTLEMENTS) {
        // Good enough ground under their feet beats a distant rumour.
        foundSettlement(pf, ws, b, cell, now);
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
inline void maybeSplit(population::Field& pf, technology::WorldState& ws, int si, double now) {
    using namespace population;
    Settlement& s = pf.settlements[si];
    float keff = technology::effectiveK(s, now);
    float phi = s.P > 1 ? keff * s.R / s.P : 2.0f;
    if (phi >= SPLIT_PHI) {
        s.scarceSince = -1;
        return;
    }
    if (s.P < SPLIT_MIN_P) return;
    if (s.scarceSince < 0) {
        s.scarceSince = now;
        return;
    }
    if (now - s.scarceSince < SPLIT_AFTER_DAYS || (int)pf.bands.size() >= MAX_BANDS) return;
    terrain::V3 home = cellCentre(s.cell);
    int tgt = bestProspect(pf, home, ws.rng);
    s.scarceSince = now; // whether or not anyone leaves, the pressure resets
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
    b.aware = s.aware;
    b.practising = s.practising;
    b.practiceT = s.practiceT;
    s.P -= b.P;
    s.S -= b.S;
    pf.bands.push_back(b);
    logAt("split from settlement", si, home, b.P, now);
}

// Process every due event in chronological order: settlement re-evaluations
// (with split checks), contact draws, the world invention clock, and band
// steps. Order matters because each event changes the rates around it.
inline bool simulate(population::Field& pf, technology::WorldState& ws, double now) {
    if (pf.settlements.empty()) return false;
    bool changed = false;
    while (true) {
        double t = ws.nextEvent;
        int kind = 0, idx = -1;
        for (int i = 0; i < (int)pf.settlements.size(); i++) {
            const population::Settlement& s = pf.settlements[i];
            if (s.nextUpdate < t) { t = s.nextUpdate; kind = 1; idx = i; }
            if (s.nextTech < t) { t = s.nextTech; kind = 2; idx = i; }
        }
        for (int i = 0; i < (int)pf.bands.size(); i++)
            if (pf.bands[i].nextUpdate < t) { t = pf.bands[i].nextUpdate; kind = 3; idx = i; }
        if (t > now) break;
        if (kind == 1) {
            population::Settlement& s = pf.settlements[idx];
            changed |= population::advance(s, technology::effectiveK(s, t), t);
            maybeSplit(pf, ws, idx, t);
        } else if (kind == 2) {
            population::Settlement& s = pf.settlements[idx];
            if (!s.techFires) { technology::redraw(pf, idx, ws, t); continue; }
            if (!s.aware) {
                s.aware = true;
                fprintf(stderr, "tech: settlement %d aware of farming, day %.0f\n", idx, t);
                technology::redraw(pf, idx, ws, t);
                for (int j : pf.neighbours[idx])
                    if (!pf.settlements[j].aware) technology::redraw(pf, j, ws, t);
            } else {
                technology::startPractising(pf, idx, ws, t);
                fprintf(stderr, "tech: settlement %d starts farming, day %.0f\n", idx, t);
            }
            technology::scheduleInvention(pf, ws, t);
            changed = true;
        } else if (kind == 3) {
            stepBand(pf, ws, idx, t);
            changed = true;
        } else {
            if (!ws.fires) { technology::scheduleInvention(pf, ws, t); continue; }
            int wi = technology::pickInventor(pf, ws, t);
            if (wi >= 0) {
                technology::startPractising(pf, wi, ws, t);
                fprintf(stderr, "tech: farming invented at settlement %d, day %.0f\n", wi, t);
                changed = true;
            }
            technology::scheduleInvention(pf, ws, t);
        }
    }
    return changed;
}

} // namespace sim
