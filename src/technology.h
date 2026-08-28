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
// Need-driven discovery (farming, granaries): desperation invents. One tough
// winter changes nobody's lifestyle, so need ramps in only after the state
// has held a year and saturates at four. Each unaware settlement contributes
// ramp x suitability x capped population; the world rate is sqrt(sum) /
// NEED_MEAN_YEARS -- sublinear, so more potential inventors invent sooner
// but a crowded hungry world is not instant. Expected mean times are pinned
// in Design/Technology.md (calibration table) -- keep them in sync.
constexpr double NEED_MEAN_YEARS = 2000.0;   // mean at total need weight 1
constexpr double NEED_RESAMPLE = 5.0 * YEAR; // need drifts yearly: short horizon
constexpr float NEED_YEARS_ON = 1.0f;        // below this, no desperation
constexpr float NEED_YEARS_SAT = 4.0f;       // full desperation

inline const char* techName(int t) {
    static const char* names[population::NTECH] = {"farming", "husbandry", "granaries",
                                                   "archery"};
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
    double nextEvent[population::NTECH] = {INF_T, INF_T, INF_T, INF_T}; // fire or resample
    bool fires[population::NTECH] = {false, false, false, false};
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
// Granaries: anyone who stores food can invent them (pre-agricultural
// granaries are real), but a harvest pulse makes the need obvious --
// practising farming multiplies the invention weight and adoption rate ~7x.
inline float suitability(const population::Settlement& s, int tech) {
    if (tech == population::TECH_FARMING) return s.sFarm;
    if (tech == population::TECH_HUSBANDRY) return s.pasture;
    if (tech == population::TECH_ARCHERY) return 1.0f; // everyone can draw a bow
    return s.tech[population::TECH_FARMING].practising ? 1.0f : 0.15f;
}

// Annual food capacity: foraging scaled by the seasonal mean (with the
// game-borne share tracking the regional pool's health), farming's
// harvest-shaped total, the herd's current flow (seasonal mean ~0.85), and
// the farmyard bonus; water caps the whole.
inline float effectiveK(const population::Settlement& s, double now) {
    float farmMult = 1.0f + FARM_YIELD_GAIN * s.sFarm * expertise(s.tech[population::TECH_FARMING], now);
    float hExp = expertise(s.tech[population::TECH_HUSBANDRY], now);
    float husb = s.herd * 0.85f + population::FARMYARD_SHARE_POP * s.kFoodP * hExp;
    float archExp = expertise(s.tech[population::TECH_ARCHERY], now);
    float cover = population::bowCoverage(s.bows, s.P);
    float bigEff = population::huntEff(s.gameNow) *
                   (1.0f + population::BOW_BIG_GAIN * cover * archExp);
    float forage = s.kFoodP - s.kGame - s.kSmall + s.kGame * bigEff +
                   s.kSmall * population::smallGameEff(cover, archExp);
    return std::min(forage * s.meanF + s.kFoodP * (farmMult - 1.0f) + husb, s.kWater);
}

// Which technologies are invented from need rather than serendipity: nobody
// farms or builds granaries unless they have to. Husbandry (taming what is
// already around you) stays on the serendipity clock.
inline bool needDriven(int tech) {
    return tech == population::TECH_FARMING || tech == population::TECH_GRANARY;
}

// One settlement's contribution to a need-driven invention rate, and its
// pick weight when the clock fires. Farming's sustained state is hunger
// (hungrySince, independent of split resets); granaries' is the storage
// fill signal holding in consecutive years (granNeedYrs).
inline float needWeight(const population::Settlement& s, int tech, double now) {
    if (s.tech[tech].aware || s.P <= 1) return 0.0f;
    float years = tech == population::TECH_FARMING
                      ? (s.hungrySince >= 0 ? (float)((now - s.hungrySince) / YEAR) : 0.0f)
                      : s.granNeedYrs;
    float acute =
        std::clamp((years - NEED_YEARS_ON) / (NEED_YEARS_SAT - NEED_YEARS_ON), 0.0f, 1.0f);
    return acute * suitability(s, tech) * std::min(s.P / 300.0f, 3.0f);
}

// Adoption need: nobody changes a working lifestyle. A settlement expanding
// at its maximum rate (phi >= PHI_CONTENT, where growth saturates) gets no
// utility from more food and does not adopt; the adoption rate then grows
// with the size of the need (population::needRamp -- the same curve that
// buys firelight work hours), reaching full speed at the invention-hunger
// threshold. Granaries use their own utility signal: the fill cycle binding.
inline float adoptionNeed(const population::Settlement& s, int tech, double now) {
    if (tech == population::TECH_GRANARY) return std::clamp(s.granNeedYrs, 0.0f, 1.0f);
    float phi = s.P > 1 ? effectiveK(s, now) * s.R / s.P : 2.0f;
    return population::needRamp(phi);
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
        // No teachers or no suitable ground parks at infinity (re-armed by
        // neighbour practice events); a contented zero need re-checks on the
        // short horizon, since contentment can end without a discrete event.
        if (esum <= 0 || suitability(s, tech) <= 0) { s.nextTech[tech] = INF_T; return; }
        double rate =
            suitability(s, tech) * adoptionNeed(s, tech, now) * esum / (PRACT_MEAN_YEARS * YEAR);
        double dt = rate > 0 ? expDraw(ws.rng, 1.0 / rate) : INF_T;
        s.techFires[tech] = dt <= NEED_RESAMPLE;
        s.nextTech[tech] = now + std::min(dt, NEED_RESAMPLE);
        if (ws.sink) ws.sink->techEvent(i, tech, s.nextTech[tech]);
    }
}

// Draw one technology's world invention clock. Need-driven techs: rate =
// sqrt(total need weight) / NEED_MEAN_YEARS, re-checked on a short horizon
// because need drifts yearly (a zero rate still re-checks -- desperation can
// arise between draws). Serendipity techs: rate = (unaware population share)
// / 10000 years.
inline void scheduleInvention(population::Field& pf, WorldState& ws, int tech, double now) {
    if (needDriven(tech)) {
        double wsum = 0;
        bool anyUnaware = false;
        for (const population::Settlement& s : pf.settlements) {
            if (!s.tech[tech].aware) anyUnaware = true;
            wsum += needWeight(s, tech, now);
        }
        if (!anyUnaware) { ws.nextEvent[tech] = INF_T; return; }
        double rate = std::sqrt(wsum) / (NEED_MEAN_YEARS * YEAR);
        double dt = rate > 0 ? expDraw(ws.rng, 1.0 / rate) : INF_T;
        ws.fires[tech] = dt <= NEED_RESAMPLE;
        ws.nextEvent[tech] = now + std::min(dt, NEED_RESAMPLE);
        if (ws.sink) ws.sink->clockEvent(tech, ws.nextEvent[tech]);
        return;
    }
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
    // Taking up farming raises granary suitability ~7x: redraw that clock so
    // the new rate applies now rather than at the next resample.
    if (tech == TECH_FARMING) redraw(pf, i, ws, TECH_GRANARY, now);
    for (int j : pf.neighbours[i]) redraw(pf, j, ws, tech, now);
}

// Weighted pick of the inventor among the unaware. Need-driven techs draw
// proportional to the same need weights that set the rate; serendipity techs
// favour big, hungry settlements on suitable ground.
inline int pickInventor(population::Field& pf, WorldState& ws, int tech, double now) {
    std::vector<double> wts(pf.settlements.size(), 0.0);
    double wsum = 0;
    for (size_t i = 0; i < pf.settlements.size(); i++) {
        const population::Settlement& s = pf.settlements[i];
        if (needDriven(tech)) {
            wts[i] = needWeight(s, tech, now);
            wsum += wts[i];
            continue;
        }
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
    // Archery is not invented here: the bow is far older than anything else
    // this simulation models, so everyone starts knowing it, at the base
    // expertise. What varies from place to place is bows, not knowing how.
    for (population::Settlement& s : pf.settlements) {
        s.tech[population::TECH_ARCHERY] = {true, true, now};
        s.nextTech[population::TECH_ARCHERY] = INF_T;
    }
    for (int t = 0; t < population::NTECH; t++) {
        for (int i = 0; i < (int)pf.settlements.size(); i++) redraw(pf, i, ws, t, now);
        scheduleInvention(pf, ws, t, now);
    }
}

} // namespace technology
