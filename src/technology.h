// Technology: what settlements know how to do. A world-level invention clock
// per technology decides when it first appears; need decides who gets it;
// awareness and practice spread by contact. Rules in Design/Technology.md.
#pragma once
#include "population.h"
#include <cmath>
#include <cstdio>

namespace technology {

constexpr double YEAR = 365.0;
constexpr double INF_T = 1e18;
// Pinned in Design/Technology.md.
constexpr double INVENT_MEAN_YEARS = 10000.0; // world mean while nobody knows
constexpr double AWARE_MEAN_YEARS = 25.0;     // per knowing neighbour
constexpr double PRACT_MEAN_YEARS = 100.0;    // per unit neighbour expertise at suitability 1
constexpr float EXPERTISE_START = 0.2f;
constexpr double EXPERTISE_TAU = 50.0 * YEAR; // practice matures toward 1
constexpr float FARM_YIELD_GAIN = 4.0f;       // food multiplier 1 + gain*s*expertise
// Herds: living stock. Growth toward a pasture cap; a seed herd is bred from
// wild capture when practice begins. Units are "people fed per day".
constexpr float HERD_SEED = 1.0f;             // bred from wild capture at practice start
// Clocks whose rates drift are redrawn at this horizon; the exponential is
// memoryless, so redrawing is exact for piecewise-constant rates.
constexpr double RESAMPLE = 50.0 * YEAR;

inline const char* techName(int t) {
    static const char* names[population::NTECH] = {"farming", "husbandry"};
    return names[t];
}

struct WorldState {
    // The simulate loop registers a sink so rescheduled clocks and contact
    // draws land in its priority queue instead of requiring O(N) scans.
    struct Sink {
        virtual void techEvent(int idx, int tech, double when) = 0;
        virtual void clockEvent(int tech, double when) = 0;
    };
    uint64_t rng = 0;
    double nextEvent[population::NTECH] = {INF_T, INF_T}; // fire or resample moment
    bool fires[population::NTECH] = {false, false};
    Sink* sink = nullptr;
};

// splitmix64, uniform in (0,1]
inline double urand(uint64_t& state) {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return ((z >> 11) + 1.0) * (1.0 / 9007199254740992.0);
}

inline double expDraw(uint64_t& rng, double meanDays) { return -meanDays * std::log(urand(rng)); }

inline float expertise(const population::TechState& ts, double now) {
    if (!ts.practising) return 0.0f;
    return 1.0f - (1.0f - EXPERTISE_START) * (float)std::exp(-(now - ts.practiceT) / EXPERTISE_TAU);
}

// The suitability gate for practising a technology at a settlement's site.
inline float suitability(const population::Settlement& s, int tech) {
    return tech == population::TECH_FARMING ? s.sFarm : s.pasture;
}

// Annual food capacity: foraging scaled by the seasonal mean, farming's
// harvest-shaped total, the herd's current flow (seasonal mean ~0.85), and
// the farmyard bonus; water caps the whole.
inline float effectiveK(const population::Settlement& s, double now) {
    float farmMult = 1.0f + FARM_YIELD_GAIN * s.sFarm * expertise(s.tech[population::TECH_FARMING], now);
    float hExp = expertise(s.tech[population::TECH_HUSBANDRY], now);
    float husb = s.herd * 0.85f + population::FARMYARD_SHARE_POP * s.kFoodP * hExp;
    return std::min(s.kFoodP * (s.meanF + farmMult - 1.0f) + husb, s.kWater);
}

// Redraw a settlement's next contact event for one technology.
inline void redraw(population::Field& pf, int i, WorldState& ws, int tech, double now) {
    using namespace population;
    Settlement& s = pf.settlements[i];
    TechState& ts = s.tech[tech];
    if (ts.practising) { s.nextTech[tech] = INF_T; return; }
    if (!ts.aware) {
        int knowing = 0;
        for (int j : pf.neighbours[i]) knowing += pf.settlements[j].tech[tech].aware ? 1 : 0;
        if (!knowing) { s.nextTech[tech] = INF_T; return; }
        s.nextTech[tech] = now + expDraw(ws.rng, AWARE_MEAN_YEARS * YEAR / knowing);
        s.techFires[tech] = true;
        if (ws.sink) ws.sink->techEvent(i, tech, s.nextTech[tech]);
    } else {
        float esum = 0;
        for (int j : pf.neighbours[i]) esum += expertise(pf.settlements[j].tech[tech], now);
        double rate = suitability(s, tech) * esum / (PRACT_MEAN_YEARS * YEAR);
        if (rate <= 0) { s.nextTech[tech] = INF_T; return; }
        double dt = expDraw(ws.rng, 1.0 / rate);
        s.techFires[tech] = dt <= RESAMPLE;
        s.nextTech[tech] = now + std::min(dt, RESAMPLE);
        if (ws.sink) ws.sink->techEvent(i, tech, s.nextTech[tech]);
    }
}

// Draw one technology's world invention clock: rate = (unaware population
// share) / 10000 years.
inline void scheduleInvention(population::Field& pf, WorldState& ws, int tech, double now) {
    double unaware = 0, total = 0;
    for (const population::Settlement& s : pf.settlements) {
        total += s.P;
        if (!s.tech[tech].aware) unaware += s.P;
    }
    double rate = total > 0 ? unaware / total / (INVENT_MEAN_YEARS * YEAR) : 0;
    if (rate <= 0) { ws.nextEvent[tech] = INF_T; return; }
    double dt = expDraw(ws.rng, 1.0 / rate);
    ws.fires[tech] = dt <= RESAMPLE;
    ws.nextEvent[tech] = now + std::min(dt, RESAMPLE);
    if (ws.sink) ws.sink->clockEvent(tech, ws.nextEvent[tech]);
}

inline void startPractising(population::Field& pf, int i, WorldState& ws, int tech, double now) {
    using namespace population;
    Settlement& s = pf.settlements[i];
    s.tech[tech].aware = true;
    s.tech[tech].practising = true;
    s.tech[tech].practiceT = now;
    s.nextTech[tech] = INF_T;
    if (tech == TECH_HUSBANDRY) s.herd = std::max(s.herd, HERD_SEED); // bred from capture
    for (int j : pf.neighbours[i]) redraw(pf, j, ws, tech, now);
}

// Weighted pick of the inventor among the unaware: big, hungry settlements on
// suitable ground invent.
inline int pickInventor(population::Field& pf, WorldState& ws, int tech, double now) {
    std::vector<double> wts(pf.settlements.size(), 0.0);
    double wsum = 0;
    for (size_t i = 0; i < pf.settlements.size(); i++) {
        const population::Settlement& s = pf.settlements[i];
        float suit = suitability(s, tech);
        if (s.tech[tech].aware || suit <= 0 || s.P <= 1) continue;
        float phi = effectiveK(s, now) * s.R / std::max(s.P, 1.0f);
        float scarcity = std::clamp((1.0f - phi) / 0.2f, 0.0f, 1.0f);
        wts[i] = s.P * suit * (1.0 + 9.0 * scarcity);
        wsum += wts[i];
    }
    if (wsum <= 0) return -1;
    double pick = urand(ws.rng) * wsum;
    for (size_t i = 0; i < wts.size(); i++) {
        pick -= wts[i];
        if (pick <= 0) return (int)i;
    }
    return -1;
}

inline void init(population::Field& pf, WorldState& ws, uint32_t seed, double now) {
    ws.rng = (uint64_t)seed * 0x2545F4914F6CDD1Dull + 0x853C49E6748FEA9Bull;
    for (int t = 0; t < population::NTECH; t++) {
        for (int i = 0; i < (int)pf.settlements.size(); i++) redraw(pf, i, ws, t, now);
        scheduleInvention(pf, ws, t, now);
    }
}

} // namespace technology
