// The event loop (Design/Event-Driven.md): every due event in strict
// chronological order, since each can change the rates of the others --
// settlement wakes, contact draws, the invention clocks, band steps, the
// game-pool tick -- then a catch-up that brings the whole world current to
// the displayed moment. The rules each event applies live in the headers
// included below; this file only decides what runs when.
#pragma once
#include "technology.h"
#include "events.h"
#include "sphere.h"
#include "claims.h"
#include "farmland.h"
#include "raids.h"
#include "bands.h"
#include "atmosphere.h"
#include <cmath>
#include <cstdio>
#include <queue>

namespace sim {

// Advance every regional game pool to `now` (population.h constants). The
// draw is what the region's settlements currently eat from the game side of
// their diet; depletion is proportional to actual kills, recovery is slow,
// and below the Allee floor there is no recovery at all. Runs on a fixed
// 90-day schedule (plus catch-up), so it is step-size invariant. Bands are
// too small and transient to count.
inline void gameTick(population::Field& pf, double now) {
    double dt = now - pf.gameT;
    if (dt <= 0 || pf.gameG.empty()) return;
    std::vector<float> draw(pf.gameG.size(), 0.0f);
    for (const population::Settlement& s : pf.settlements) {
        if (s.kGame <= 0 || s.P <= 1) continue;
        population::SeasonCtx ctx = technology::annualCtx(s, now);
        population::FoodTerms f = population::foodTerms(s, ctx);
        // Deviation from "the diet is defined once" (foodTerms): the herds'
        // flow is multiplied here in the order this function always used,
        // (kGame * huntEff) * bows * meanF, not f.bigGame * meanF, which
        // groups the same factors as kGame * (huntEff * bows). The two
        // differ by a rounding, and that rounding moved a 40-year probe by
        // one person; the order stays until a behaviour change is wanted.
        float g = pf.gameG[s.gRegion];
        float gameFlow = s.kGame * population::huntEff(g) *
                         (1.0f + population::BOW_BIG_GAIN * ctx.bowCover * ctx.archExp) * s.meanF;
        float total =
            f.plant * s.meanF + f.smallGame * s.meanF + gameFlow + f.farm + f.herd + f.farmyard;
        if (total <= 1e-6f) continue;
        draw[s.gRegion] += s.P * gameFlow / total; // game share of what they eat
    }
    for (size_t r = 0; r < pf.gameG.size(); r++) {
        if (pf.gameDmax[r] <= 0) continue;
        float g = pf.gameG[r];
        float regen = g >= population::GAME_FLOOR
                          ? (1.0f - g) / (population::GAME_REGEN_YEARS * 365.0f)
                          : 0.0f;
        float depl = draw[r] / pf.gameDmax[r] / (population::GAME_DEPLETE_YEARS * 365.0f);
        float before = g;
        pf.gameG[r] = std::clamp(g + (regen - depl) * (float)dt, 0.0f, 1.0f);
        if (before >= population::GAME_FLOOR && pf.gameG[r] < population::GAME_FLOOR)
            note(pf, population::EV_GAME_GONE, now, 0, 0, 0, 0,
                 "a regional herd was hunted past saving");
    }
    for (population::Settlement& s : pf.settlements)
        if (s.kGame > 0) s.gameNow = pf.gameG[s.gRegion];
    pf.gameT = now;
}

inline population::SeasonCtx seasonCtx(const population::Settlement& s, const hydrology::Result& hy,
                                       const atmosphere::Climatology& clim, double now) {
    population::SeasonCtx ctx = technology::annualCtx(s, now);
    ctx.clim = &clim;
    ctx.n = cellCentre(s.cell);
    ctx.h = std::max(hy.heightM[s.cell], 0.0f);
    return ctx;
}

// Erase the settlements that walked away this step and weather old ruins.
// Runs once, after the event loop, so indices stay valid while events are
// being processed; panels track settlements by id, not index.
inline void sweepDeparted(population::Field& pf, double now) {
    for (int i = (int)pf.ruins.size() - 1; i >= 0; i--)
        if (now - pf.ruins[i].abandoned > population::RUIN_LIFE_DAYS)
            pf.ruins.erase(pf.ruins.begin() + i);
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
    std::vector<population::Settlement> keep;
    keep.reserve(k);
    for (int i = 0; i < n; i++)
        if (nu[i] >= 0) keep.push_back(pf.settlements[i]);
    pf.settlements.swap(keep);
    pf.neighbours.swap(nb);
}

// What the queue holds. Each entry is (time, kind, id); a pop is validated
// against the authoritative next-time and a stale entry is skipped.
enum class Due {
    InventionClock, // the world clock of one technology (tech)
    SettlementWake, // a settlement's scheduled re-evaluation (idx)
    ContactDraw,    // a settlement's contact draw for one technology (idx, tech)
    BandStep,       // a band's next step (idx = band id, stable across erases)
    GameTick        // the regional game pools' fixed-cadence update
};

struct Ev {
    double t;
    Due kind;
    int idx, tech;
    bool operator<(const Ev& o) const { return t > o.t; } // min-heap
};

// Process every due event in chronological order through a lazy priority
// queue, since each event changes the rates around it. After the loop, a
// catch-up pass brings every settlement and band current to `now` -- any
// step size leaves the whole world exact at the displayed moment.
inline bool simulate(population::Field& pf, technology::WorldState& ws, const hydrology::Result& hy,
                     const atmosphere::Climatology& clim, double now) {
    if (pf.settlements.empty()) return false;
    bool changed = false;

    std::priority_queue<Ev> q;

    struct HeapSink : technology::WorldState::Sink {
        std::priority_queue<Ev>* q;
        void techEvent(int idx, int tech, double when) override {
            if (when < 1e17) q->push({when, Due::ContactDraw, idx, tech});
        }
        void clockEvent(int tech, double when) override {
            if (when < 1e17) q->push({when, Due::InventionClock, 0, tech});
        }
    } sink;
    sink.q = &q;
    ws.sink = &sink;

    auto pushSettlement = [&](int i) {
        const population::Settlement& s = pf.settlements[i];
        if (s.nextUpdate < 1e17) q.push({s.nextUpdate, Due::SettlementWake, i, 0});
        for (int t = 0; t < population::NTECH; t++)
            if (s.nextTech[t] < 1e17) q.push({s.nextTech[t], Due::ContactDraw, i, t});
    };
    auto pushBand = [&](const population::Band& b) {
        q.push({b.nextUpdate, Due::BandStep, (int)b.id, 0});
    };
    for (int i = 0; i < (int)pf.settlements.size(); i++) pushSettlement(i);
    for (const population::Band& b : pf.bands) pushBand(b);
    for (int t = 0; t < population::NTECH; t++)
        if (ws.nextEvent[t] < 1e17) q.push({ws.nextEvent[t], Due::InventionClock, 0, t});
    if (!pf.gameG.empty()) q.push({pf.gameT + population::GAME_TICK_DAYS, Due::GameTick, 0, 0});

    while (!q.empty() && q.top().t <= now) {
        pf.peakBands = std::max(pf.peakBands, pf.bands.size()); // high-water mark
        Ev ev = q.top();
        q.pop();
        double t = ev.t;
        switch (ev.kind) {
        case Due::InventionClock: {
            if (t != ws.nextEvent[ev.tech]) continue; // stale
            if (!ws.fires[ev.tech]) {
                technology::scheduleInvention(pf, ws, ev.tech, t);
                continue;
            }
            int wi = technology::pickInventor(pf, ws, ev.tech, t);
            if (wi >= 0) {
                technology::startPractising(pf, wi, ws, ev.tech, t);
                {
                    char txt[96];
                    snprintf(txt, sizeof txt, "%s invented %s!", pf.settlements[wi].name,
                             technology::techName(ev.tech));
                    note(pf, population::EV_INVENTED, t, pf.settlements[wi].id, 0, 0,
                         (float)ev.tech, txt);
                }
                fprintf(stderr, "tech: %s invented at settlement %d, day %.0f\n",
                        technology::techName(ev.tech), wi, t);
                changed = true;
            }
            technology::scheduleInvention(pf, ws, ev.tech, t);
            break;
        }
        case Due::SettlementWake: {
            population::Settlement& s = pf.settlements[ev.idx];
            if (t != s.nextUpdate) continue;
            technology::decaySkills(pf, ws, ev.idx, t);
            growClaim(pf, ev.idx, t);
            updateFarmland(pf, s); // the claim moved or a farmstead finished
            changed |=
                population::advance(s, technology::effectiveK(s, t), seasonCtx(s, hy, clim, t), t);
            reportGranaries(pf, s, t);
            size_t bandsBefore = pf.bands.size();
            // Decide first: leaving or growing scarce can pull the next wake
            // earlier, and the queue entry must carry the final time.
            maybeRelocateOrSplit(pf, ws, hy, clim, ev.idx, t);
            if (s.nextUpdate < 1e17) q.push({s.nextUpdate, Due::SettlementWake, ev.idx, 0});
            for (size_t b = bandsBefore; b < pf.bands.size(); b++) pushBand(pf.bands[b]);
            break;
        }
        case Due::ContactDraw: {
            population::Settlement& s = pf.settlements[ev.idx];
            if (t != s.nextTech[ev.tech]) continue;
            if (!s.techFires[ev.tech]) {
                technology::redraw(pf, ev.idx, ws, ev.tech, t);
                continue;
            }
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
                    note(pf, population::EV_ADOPTED, t, s.id, 0, 0, (float)ev.tech, txt);
                }
                fprintf(stderr, "tech: settlement %d starts %s, day %.0f\n", ev.idx,
                        technology::techName(ev.tech), t);
            }
            technology::scheduleInvention(pf, ws, ev.tech, t);
            changed = true;
            break;
        }
        case Due::GameTick: {
            if (std::fabs(t - (pf.gameT + population::GAME_TICK_DAYS)) > 1e-6) continue; // stale
            gameTick(pf, t);
            q.push({pf.gameT + population::GAME_TICK_DAYS, Due::GameTick, 0, 0});
            break;
        }
        case Due::BandStep: {
            int bi = -1;
            for (int i = 0; i < (int)pf.bands.size(); i++)
                if ((int)pf.bands[i].id == ev.idx) {
                    bi = i;
                    break;
                }
            if (bi < 0 || t != pf.bands[bi].nextUpdate) continue;
            size_t settsBefore = pf.settlements.size();
            bool alive = stepBand(pf, ws, hy, clim, bi, t);
            if (alive) pushBand(pf.bands[bi]);
            for (size_t i = settsBefore; i < pf.settlements.size(); i++) pushSettlement((int)i);
            changed = true;
            break;
        }
        }
    }
    ws.sink = nullptr;

    // Catch-up: bring every settlement and band current to `now`, whatever
    // the step size -- minute steps show the world in full detail, big steps
    // aggregate through the event loop above first (Design/Event-Driven).
    for (int i = 0; i < (int)pf.settlements.size(); i++) {
        population::Settlement& s = pf.settlements[i];
        if (!s.leaving && s.t < now - 1e-9) {
            technology::decaySkills(pf, ws, i, now);
            growClaim(pf, i, now);
            updateFarmland(pf, s);
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
