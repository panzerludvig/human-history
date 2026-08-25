// Population: a carrying-capacity field derived from terrain and water, and
// settlements — sparse actors with people P and land condition R that evolve
// by scheduled re-evaluations, not ticks. See Design/Population.md.
#pragma once
#include "terrain.h"
#include "hydrology.h"
#include "atmosphere.h"
#include <cstdio>
#include <vector>
#include <algorithm>

namespace population {

constexpr int W = hydrology::W, H = hydrology::H;

// Rules (Design/Population.md).
constexpr float FORAGE_KM2 = 314.0f;          // 10 km radius disc
constexpr float WATER_L_PER_PERSON = 20.0f;   // per day
constexpr float USABLE_WATER = 0.05f;         // fraction of discharge usable
constexpr float GROWTH_MAX = 0.028f;          // per year at full surplus
constexpr float DECLINE_MAX = 0.07f;          // per year in famine
// The land timescales are slow relative to growth so populations overshoot
// visibly (~18% peak around year 33 from a half-capacity start) before the
// land's decline pulls them back. Their ratio (1.5) fixes R* = 0.549.
constexpr float R_REGEN_YEARS = 33.0f;        // land recovery
constexpr float R_DEPLETE_YEARS = 22.0f;      // land depletion at P = K
// Land condition settles where regeneration balances depletion:
// (1-R)/T_regen = R^2/T_deplete, giving R* = 0.549 for 12 and 8 years.
// The yield table is measured *sustained* density, so the pristine ceiling
// stored in K is table / R*; displayed capacity is K * R*.
constexpr float SUSTAIN_R = 0.5486f;
constexpr float MIN_SETTLEMENT_K = 150.0f;
constexpr int MAX_SETTLEMENTS = 400;        // initial placement
constexpr int MAX_TOTAL_SETTLEMENTS = 8000; // founding stops at geography, not here

// Storage and famine (Design/Migration.md). The land offers a flow (K*R
// rations/day); the group holds a stock S of harvested rations. Famine is not
// a hard breakpoint: hoarding excludes people from low stores before they
// empty, so deaths = STARVE_MAX * excluded share * harvest shortfall.
constexpr float GATHER_SETTLED = 1.5f;   // rations/person/day gatherable settled
constexpr float GATHER_MOVING = 0.5f;    // a moving band forages on a third of its time
constexpr float CAP_DAYS_SETTLED = 90.0f;
constexpr float CAP_DAYS_BAND = 10.0f;
constexpr float HOARD_FILL = 0.25f;      // famine sets in below this fill fraction
constexpr float STARVE_MAX = 0.02f;      // /day at full exclusion and total shortfall

// Splitting and bands (Design/Migration.md).
// Just under the phi = 1 equilibrium: the overshoot decline glides at
// phi ~ 0.97-0.99, so a deeper threshold would never fire.
constexpr float SPLIT_PHI = 0.99f;
constexpr double SPLIT_AFTER_DAYS = 730.0;
constexpr float SPLIT_MIN_P = 50.0f;
constexpr float SPLIT_SHARE = 1.0f / 3.0f;
constexpr float BAND_MIN_P = 20.0f;
constexpr float BAND_SPEED_KM_DAY = 15.0f;
// Awareness (Design/Migration.md): a base radius, growth with settled age
// (saturating -- the marginal new ground per year shrinks), a scouting bonus
// for resting bands, and a vantage bonus from prominence via the real
// horizon formula. One function each; the shader receives the result.
constexpr float AWARE_BASE_KM = 150.0f;
constexpr float AWARE_GROWTH_KM = 300.0f;      // settlements, toward base+this
constexpr double AWARE_TAU_DAYS = 30.0 * 365;  // settlement growth timescale
constexpr float AWARE_REST_KM = 100.0f;        // resting bands, toward base+this
constexpr double AWARE_REST_TAU_DAYS = 45.0;
constexpr float AWARE_CAP_KM = 600.0f;
constexpr double BAND_STEP_DAYS = 5.0;

inline float vantageKm(float promM) { return 3.57f * std::sqrt(std::max(promM, 0.0f)); }

inline float settlementAwareKm(double ageDays, float promM) {
    float r = AWARE_BASE_KM + AWARE_GROWTH_KM * (1.0f - (float)std::exp(-ageDays / AWARE_TAU_DAYS));
    return std::min(r + vantageKm(promM), AWARE_CAP_KM);
}

inline float bandAwareKm(double restDays, float promM) {
    float r = AWARE_BASE_KM + AWARE_REST_KM * (1.0f - (float)std::exp(-restDays / AWARE_REST_TAU_DAYS));
    return std::min(r + vantageKm(promM), AWARE_CAP_KM);
}
constexpr int MAX_BANDS = 200;
constexpr float FARMYARD_SHARE_POP = 0.05f; // household animals, no pasture needed
constexpr float HERD_GROWTH_YR = 0.25f;     // logistic growth rate
constexpr float HERD_PASTURE_K = 2.0f;      // people/km2 on pure pasture at full expertise

// The technology table (Design/Technology.md): per-settlement state for each
// technology. Farming's original fields generalized when husbandry arrived.
enum : int { TECH_FARMING = 0, TECH_HUSBANDRY = 1, NTECH = 2 };
struct TechState {
    bool aware = false;
    bool practising = false; // implies aware
    double practiceT = 0;    // sim day practice began (expertise grows from here)
};

struct Settlement {
    int cell;
    float P;           // people
    float R;           // land condition 0..1
    double t;          // sim day at which P and R are valid
    double nextUpdate; // sim day of the next scheduled re-evaluation
    float S = 0;               // food store, rations (person-days)
    double scarceSince = -1;   // sim day scarcity began, -1 if fed (split rule)
    double founded = 0;        // sim day the settlement was founded (awareness age)
    // Fixed local properties (from the terrain at the cell):
    float kFoodP = 0;  // pristine food capacity (already / SUSTAIN_R)
    float meanF = 1;   // annual mean forage factor (seasonal climate)
    float meanG2 = 1;  // annual mean squared growing activity (farming shape)
    float tSeason[4] = {15, 15, 15, 15}; // season temps at the site (cached)
    float kWater = 0;  // water-supply capacity
    float sFarm = 0;   // farming suitability 0..1 (grass-like cover, warm enough)
    float pasture = 0; // grazing suitability 0..1 (grass, steppe, savanna, some tundra)
    float herd = 0;    // livestock, in people-fed-per-day units (husbandry)
    // Technology state (see technology.h / Design/Technology.md):
    TechState tech[NTECH];
    double nextTech[NTECH] = {1e18, 1e18}; // next contact draw or resample moment
    bool techFires[NTECH] = {false, false};
};

// A migrating group: a settlement with velocity (Design/Migration.md). It
// forages the cell it stands on with a reduced time budget, carries a small
// store, and re-evaluates every few days. The first agent.
struct Band {
    uint32_t id = 0;         // stable identity (indices shift as bands die)
    float px, py, pz;        // unit-sphere position
    float P = 0;             // people
    float S = 0;             // food store, rations
    int targetCell = -1;     // rough destination (a rumour, re-checked up close)
    bool resting = false;    // stopped to refill the store
    double restStart = 0;
    double t = 0;            // sim day at which the state is valid
    double nextUpdate = 0;
    // Technology carried (demic diffusion):
    TechState tech[NTECH];
};

struct Field {
    std::vector<float> K;          // carrying capacity per cell (people), 0 on water
    std::vector<int> settlementAt; // settlement index per cell, -1 none
    std::vector<Settlement> settlements;
    std::vector<std::vector<int>> neighbours; // settlements within contact range
    std::vector<Band> bands;
    uint32_t nextBandId = 1;
    // Per-cell local properties, kept for founding settlements at runtime:
    std::vector<float> kFoodPMap, kWaterMap, sFarmMap, pastureMap;
};

constexpr float CONTACT_KM = 160.0f; // twice the minimum settlement spacing

inline void computeNeighbours(Field& f) {
    auto cellN = [](int cell) {
        hydrology::V3orig d = hydrology::cellDir(cell % W, cell / W);
        return terrain::V3{d.x, d.y, d.z};
    };
    int n = (int)f.settlements.size();
    f.neighbours.assign(n, {});
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            float d = std::acos(std::clamp(terrain::dot(cellN(f.settlements[i].cell),
                                                        cellN(f.settlements[j].cell)),
                                           -1.0f, 1.0f)) * 6371.0f;
            if (d <= CONTACT_KM) {
                f.neighbours[i].push_back(j);
                f.neighbours[j].push_back(i);
            }
        }
}

// Natural food yield, people per km^2 at land condition R = 1, by cover class:
// bare, tundra, taiga, forest, rainforest, grass, steppe, savanna, shrub, marsh, desert.
inline float coverYield(const terrain::Mixture& m) {
    static const float y[terrain::NCOV] = {0.0f, 0.08f, 0.4f, 1.2f, 1.0f, 0.8f, 0.3f, 0.6f, 0.2f, 1.0f, 0.02f};
    float d = 0;
    for (int i = 0; i < terrain::NCOV; i++) d += m.cov[i] * y[i];
    return d;
}

// Farming suitability: the grass-like share of the cover (grass, steppe,
// savanna, marsh at half credit -- the real cradles were river floodplains)
// times a warmth window. You can't domesticate what doesn't grow around you.
inline float farmSuitability(const terrain::Mixture& m, float tempC) {
    float grassy = m.cov[5] + m.cov[6] + m.cov[7] + 0.5f * m.cov[9];
    return grassy * std::clamp(tempC / 8.0f, 0.0f, 1.0f);
}

// Grazing suitability: what herds can eat. No warmth gate -- cold-steppe and
// tundra herding (reindeer) are real.
inline float pastureSuitability(const terrain::Mixture& m) {
    return std::min(m.cov[5] + m.cov[6] + m.cov[7] + 0.4f * m.cov[8] + 0.3f * m.cov[1], 1.0f);
}

inline Field build(const terrain::ContinentParams& cp, float seaLevel, const float rot[9],
                   terrain::V3 offset, const plates::Field& pf, const hydrology::Result& hy,
                   const atmosphere::Climatology* clim = nullptr) {
    Field f;
    f.K.assign(W * H, 0.0f);
    f.settlementAt.assign(W * H, -1);
    const std::vector<float>& hm = hy.heightM;

    // Everything the population model needs to know about one cell.
    auto evalCell = [&](int x, int y, float& kFoodP, float& kWater, float& sFarm, float& pasture) {
        int i = y * W + x;
        kFoodP = kWater = sFarm = pasture = 0;
        float h = hm[i];
        if (h <= 0) return;                                           // land only
        if (hy.cells[i].lakeLevel > hydrology::NO_LAKE + 1 && h < hy.cells[i].lakeLevel) return;

        hydrology::V3orig cd = hydrology::cellDir(x, y);
        terrain::V3 n = {cd.x, cd.y, cd.z};
        terrain::V3 w = terrain::rotate(rot, n) + offset;
        float lat = std::asin(std::clamp(n.z, -1.0f, 1.0f));
        float lon = std::atan2(n.y, n.x);
        atmosphere::DerivedClimate dc =
            clim ? atmosphere::deriveAt(*clim, lat, lon, w, h)
                 : atmosphere::DerivedClimate{terrain::temperatureC(lat, h),
                                              terrain::moistureAt(w, lat),
                                              terrain::temperatureC(lat, h) - 4.0f, 0.0f};
        float temp = dc.temp;
        float moist = dc.moist;
        // Coarse slope from neighbouring cell heights.
        float hx = hm[y * W + hydrology::wrapX(x + 1)] - hm[y * W + hydrology::wrapX(x - 1)];
        float hyv = hm[std::min(y + 1, H - 1) * W + x] - hm[std::max(y - 1, 0) * W + x];
        float cellKm = 2 * 3.14159265f * 6371.0f / W * std::max(std::cos(lat), 0.05f);
        float slope = std::sqrt(hx * hx + hyv * hyv) / (2000.0f * cellKm);
        float uplift = pf.sample({n.x, n.y, n.z}).uplift;
        terrain::Mixture m = terrain::mixtureAt(h, slope, temp, moist, uplift,
                                                hy.cells[i].nearRiver > 0.5f, terrain::patchNoise(w),
                                                dc.swamp, dc.tCold);

        // kFood is sustained yield; the pristine ceiling is higher. Water is
        // a physical daily supply and is not scaled (it rarely binds before
        // farming and irrigation).
        kFoodP = coverYield(m) * FORAGE_KM2 / SUSTAIN_R;
        // Water within reach: accKm2 is runoff-equivalent drainage area at the
        // reference runoff (hydrology::reweight), fed by the climate's rain.
        float litresPerDay = hy.accKm2[i] * hydrology::REF_RUNOFF_MM_YR * 1.0e6f / 365.0f;
        kWater = litresPerDay * USABLE_WATER / WATER_L_PER_PERSON;
        sFarm = farmSuitability(m, temp);
        pasture = pastureSuitability(m);
    };

    f.kFoodPMap.assign(W * H, 0.0f);
    f.kWaterMap.assign(W * H, 0.0f);
    f.sFarmMap.assign(W * H, 0.0f);
    f.pastureMap.assign(W * H, 0.0f);
#pragma omp parallel for
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            evalCell(x, y, f.kFoodPMap[i], f.kWaterMap[i], f.sFarmMap[i], f.pastureMap[i]);
            f.K[i] = std::min(f.kFoodPMap[i], f.kWaterMap[i]);
        }

    // Settlements at local maxima of K, best first, spaced at least ~80 km.
    struct Cand { float k; int cell; };
    std::vector<Cand> cands;
    for (int y = 2; y < H - 2; y++)
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            if (f.K[i] < MIN_SETTLEMENT_K) continue;
            bool best = true;
            for (int dy = -2; dy <= 2 && best; dy++)
                for (int dx = -2; dx <= 2 && best; dx++)
                    if (f.K[(y + dy) * W + hydrology::wrapX(x + dx)] > f.K[i]) best = false;
            if (best) cands.push_back({f.K[i], i});
        }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.k > b.k; });
    auto cellN = [](int cell) {
        hydrology::V3orig d = hydrology::cellDir(cell % W, cell / W);
        return terrain::V3{d.x, d.y, d.z};
    };
    for (const Cand& c : cands) {
        if ((int)f.settlements.size() >= MAX_SETTLEMENTS) break;
        terrain::V3 n = cellN(c.cell);
        bool clear = true;
        for (const Settlement& s : f.settlements) {
            terrain::V3 sn = cellN(s.cell);
            float d = std::acos(std::clamp(terrain::dot(n, sn), -1.0f, 1.0f)) * 6371.0f;
            if (d < 80.0f) { clear = false; break; }
        }
        if (!clear) continue;
        f.settlementAt[c.cell] = (int)f.settlements.size();
        Settlement s{c.cell, c.k * SUSTAIN_R * 0.5f, 1.0f, 0.0, 0.0};
        s.kFoodP = f.kFoodPMap[c.cell];
        s.kWater = f.kWaterMap[c.cell];
        s.sFarm = f.sFarmMap[c.cell];
        s.pasture = f.pastureMap[c.cell];
        s.S = 0.5f * CAP_DAYS_SETTLED * s.P;
        if (clim) atmosphere::seasonProfile(*clim, cellN(c.cell),
                                            std::max(hy.heightM[c.cell], 0.0f), s.tSeason,
                                            s.meanF, s.meanG2);
        f.settlements.push_back(s);
    }
    computeNeighbours(f);
    // Startup listing for testing: where the first settlements are.
    for (int i = 0; i < (int)f.settlements.size() && i < 5; i++) {
        int cx = f.settlements[i].cell % W, cy = f.settlements[i].cell / W;
        float lat = ((cy + 0.5f) / H) * 180.0f - 90.0f, lon = ((cx + 0.5f) / W) * 360.0f - 180.0f;
        fprintf(stderr, "settlement %d: lat %.2f lon %.2f K %.0f\n", i, lat, lon, f.K[f.settlements[i].cell]);
    }
    return f;
}

// Growth runs when the land's flow covers everyone; decline is famine:
// deaths need both low stores (hoarding excludes the bottom of the group) and
// an inadequate harvest flow. Calibrated offline to preserve the ~18%
// overshoot at year ~33 and settling at the sustained capacity. `flow` is
// the seasonal food flow already assembled by the caller; `capDays` grows
// with farming (granaries).
inline void derivatives(float P, float R, float S, float flow, float K, float capDays, float& dP,
                        float& dR, float& dS) {
    float H = std::min(flow, GATHER_SETTLED * P);         // limited by land and time
    float cap = capDays * std::max(P, 1.0f);
    float fill = std::clamp(S / cap, 0.0f, 1.0f);
    float excl = std::clamp(1.0f - fill / HOARD_FILL, 0.0f, 1.0f);
    float shortfall = P > 0 ? std::clamp(1.0f - H / P, 0.0f, 1.0f) : 0.0f;
    float phi = P > 1 ? flow / P : 2.0f;
    float g = phi >= 1 ? GROWTH_MAX / 365.0f * std::min((phi - 1) / 0.11f, 1.0f) : 0.0f;
    dP = P * g - STARVE_MAX * P * excl * shortfall;
    dR = (1 - R) / (R_REGEN_YEARS * 365) - (P / std::max(K, 1.0f)) * R / (R_DEPLETE_YEARS * 365);
    dS = H - P;
}

// The seasonal food flow at time t: foraging follows the forage factor,
// farming follows squared growing activity normalized to keep its annual
// total (a prominent harvest season; year-round cropping in the tropics),
// and water caps the whole.
struct SeasonCtx {
    const atmosphere::Climatology* clim = nullptr;
    terrain::V3 n{};
    float h = 0;
    float farmMult = 1; // 1 + gain*s*expertise, from technology
    float husbExp = 0;  // husbandry expertise
};

// The season-interpolated site temperature from the cached profile.
inline float cachedSeasonT(const Settlement& s, double t) {
    double sf = std::fmod(t, 365.0) / 365.0 * 4.0 - 0.5;
    int s0 = ((int)std::floor(sf) % 4 + 4) % 4, s1 = (s0 + 1) % 4;
    float f = (float)(sf - std::floor(sf));
    return s.tSeason[s0] * (1 - f) + s.tSeason[s1] * f;
}

inline float foodFlow(const Settlement& s, const SeasonCtx& ctx, float R, double t) {
    float forage = s.kFoodP, farm = s.kFoodP * (ctx.farmMult - 1.0f);
    float fF = s.meanF, fG2 = 1.0f;
    if (ctx.clim) {
        float tC = cachedSeasonT(s, t);
        fF = atmosphere::forageFactor(tC);
        float g = atmosphere::growthActivity(tC);
        fG2 = g * g / std::max(s.meanG2, 0.05f);
    }
    // Husbandry: the herd is a walking store -- its flow barely dips in
    // winter (fodder and slaughter). Plus the pasture-free farmyard animals.
    float gNow = std::clamp((fF - 0.12f) / 0.88f, 0.0f, 1.0f);
    float husb = (s.herd * (0.7f + 0.3f * gNow) +
                  FARMYARD_SHARE_POP * s.kFoodP * ctx.husbExp) * R;
    return std::min((forage * fF + farm * fG2) * R + husb, s.kWater);
}

// Integrate a settlement from its valid time to `now` and schedule the next
// re-evaluation at the moment its state will have drifted about 5%. Famine
// can move at percent-per-day, so steps stay short and the horizon also
// watches for the store crossing the hoarding threshold.
inline bool advance(Settlement& s, float K, const SeasonCtx& ctx, double now) {
    if (K <= 0) { s.t = now; s.nextUpdate = now + 3650; return false; }
    float capDays = CAP_DAYS_SETTLED * (2.0f - 1.0f / std::max(ctx.farmMult, 1.0f)); // granaries
    float herdCap = s.pasture * FORAGE_KM2 * HERD_PASTURE_K / SUSTAIN_R *
                    (0.3f + 0.7f * ctx.husbExp);
    float P = s.P, R = s.R, S = s.S;
    double span = now - s.t;
    int steps = std::clamp((int)(span / 5.0) + 1, 1, 800);
    float hstep = (float)(span / steps);
    for (int k = 0; k < steps && hstep > 0; k++) {
        double tk = s.t + (k + 0.5) * hstep;
        float dP, dR, dS;
        derivatives(P, R, S, foodFlow(s, ctx, R, tk), K, capDays, dP, dR, dS);
        P = std::max(P + dP * hstep, 0.0f);
        R = std::clamp(R + dR * hstep, 0.0f, 1.0f);
        S = std::clamp(S + dS * hstep, 0.0f, capDays * std::max(P, 1.0f));
        if (s.herd > 0 && herdCap > 0)
            s.herd = std::clamp(s.herd + HERD_GROWTH_YR / 365.0f * s.herd *
                                             (1.0f - s.herd / herdCap) * hstep,
                                0.0f, herdCap * 1.05f);
        else if (herdCap <= 0)
            s.herd = 0;
    }
    bool changed = std::fabs(P - s.P) > 0.5f || std::fabs(R - s.R) > 0.002f;
    s.P = P;
    s.R = R;
    s.S = S;
    s.t = now;
    // Horizon from the ANNUAL-MEAN flow: the seasonal oscillation is
    // recurring, so a settlement in seasonal equilibrium still sleeps long.
    float meanFlow = std::min(s.kFoodP * (s.meanF + ctx.farmMult - 1.0f) * s.R, s.kWater);
    float dP, dR, dS;
    derivatives(s.P, s.R, s.S, meanFlow, K, capDays, dP, dR, dS);
    double horizon = 1800;
    if (std::fabs(dP) > 1e-9) horizon = std::min(horizon, 0.05 * std::max(s.P, 50.0f) / std::fabs(dP));
    if (std::fabs(dR) > 1e-9) horizon = std::min(horizon, 0.05 * std::max(s.R, 0.1f) / std::fabs(dR));
    if (dS < -1e-9) {
        double toHoard = (s.S - HOARD_FILL * capDays * s.P) / -dS;
        if (toHoard > 0) horizon = std::min(horizon, std::max(toHoard, 15.0));
    }
    s.nextUpdate = now + std::max(horizon, 5.0);
    return changed;
}

} // namespace population
