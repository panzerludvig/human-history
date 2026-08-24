// Technology: what settlements know how to do. A world-level invention clock
// decides when a technology first appears; need decides who gets it; awareness
// and practice spread by contact. See Design/Technology.md.
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
constexpr double PRACT_MEAN_YEARS = 100.0;    // per unit neighbour expertise at s = 1
constexpr float EXPERTISE_START = 0.2f;
constexpr double EXPERTISE_TAU = 50.0 * YEAR; // practice matures toward 1
constexpr float FARM_YIELD_GAIN = 4.0f;       // food multiplier 1 + gain*s*expertise
// Clocks whose rates drift (expertise growth, population change) are redrawn
// at this horizon; the exponential is memoryless, so redrawing is exact for
// piecewise-constant rates.
constexpr double RESAMPLE = 50.0 * YEAR;

struct WorldState {
    uint64_t rng = 0;
    double nextEvent = INF_T; // invention clock: fire or resample moment
    bool fires = false;       // true if nextEvent is an actual invention
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

inline float expertise(const population::Settlement& s, double now) {
    if (!s.practising) return 0.0f;
    return 1.0f - (1.0f - EXPERTISE_START) * (float)std::exp(-(now - s.practiceT) / EXPERTISE_TAU);
}

// Effective pristine capacity: farming multiplies the food side of the min,
// so water starts to bind where farming succeeds.
inline float effectiveK(const population::Settlement& s, double now) {
    float mult = 1.0f + FARM_YIELD_GAIN * s.sFarm * expertise(s, now);
    return std::min(s.kFoodP * mult, s.kWater);
}

// Redraw a settlement's next contact event from its current transition:
// unaware -> aware (terrain-blind, any knowing neighbour is a source), or
// aware -> practising (needs suitable terrain and expert neighbours).
inline void redraw(population::Field& pf, int i, WorldState& ws, double now) {
    population::Settlement& s = pf.settlements[i];
    if (s.practising) { s.nextTech = INF_T; return; }
    if (!s.aware) {
        int knowing = 0;
        for (int j : pf.neighbours[i]) knowing += pf.settlements[j].aware ? 1 : 0;
        if (!knowing) { s.nextTech = INF_T; return; }
        s.nextTech = now + expDraw(ws.rng, AWARE_MEAN_YEARS * YEAR / knowing);
        s.techFires = true;
    } else {
        float esum = 0;
        for (int j : pf.neighbours[i]) esum += expertise(pf.settlements[j], now);
        double rate = s.sFarm * esum / (PRACT_MEAN_YEARS * YEAR);
        if (rate <= 0) { s.nextTech = INF_T; return; }
        double dt = expDraw(ws.rng, 1.0 / rate);
        s.techFires = dt <= RESAMPLE;
        s.nextTech = now + std::min(dt, RESAMPLE);
    }
}

// Draw the world invention clock. Rate = (unaware share of world population)
// / 10000 years: a pristine world averages exactly the mean; independent
// invention fades to zero as the technology spreads.
inline void scheduleInvention(population::Field& pf, WorldState& ws, double now) {
    double unaware = 0, total = 0;
    for (const population::Settlement& s : pf.settlements) {
        total += s.P;
        if (!s.aware) unaware += s.P;
    }
    double rate = total > 0 ? unaware / total / (INVENT_MEAN_YEARS * YEAR) : 0;
    if (rate <= 0) { ws.nextEvent = INF_T; return; }
    double dt = expDraw(ws.rng, 1.0 / rate);
    ws.fires = dt <= RESAMPLE;
    ws.nextEvent = now + std::min(dt, RESAMPLE);
}

inline void startPractising(population::Field& pf, int i, WorldState& ws, double now) {
    population::Settlement& s = pf.settlements[i];
    s.aware = true;
    s.practising = true;
    s.practiceT = now;
    s.nextTech = INF_T;
    for (int j : pf.neighbours[i]) redraw(pf, j, ws, now);
}

// Weighted pick of the inventor among the unaware: big, hungry settlements on
// good farmland invent.
inline int pickInventor(population::Field& pf, WorldState& ws, double now) {
    std::vector<double> wts(pf.settlements.size(), 0.0);
    double wsum = 0;
    for (size_t i = 0; i < pf.settlements.size(); i++) {
        const population::Settlement& s = pf.settlements[i];
        if (s.aware || s.sFarm <= 0 || s.P <= 1) continue;
        float phi = effectiveK(s, now) * s.R / s.P;
        float scarcity = std::clamp((1.0f - phi) / 0.2f, 0.0f, 1.0f);
        wts[i] = s.P * s.sFarm * (1.0 + 9.0 * scarcity);
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
    for (int i = 0; i < (int)pf.settlements.size(); i++) redraw(pf, i, ws, now);
    scheduleInvention(pf, ws, now);
}

} // namespace technology
