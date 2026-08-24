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
constexpr float KNOW_RADIUS_KM = 400.0f;
constexpr double BAND_STEP_DAYS = 5.0;
constexpr int MAX_BANDS = 200;

struct Settlement {
    int cell;
    float P;           // people
    float R;           // land condition 0..1
    double t;          // sim day at which P and R are valid
    double nextUpdate; // sim day of the next scheduled re-evaluation
    float S = 0;               // food store, rations (person-days)
    double scarceSince = -1;   // sim day scarcity began, -1 if fed (split rule)
    // Fixed local properties (from the terrain at the cell):
    float kFoodP = 0;  // pristine food capacity (already / SUSTAIN_R)
    float kWater = 0;  // water-supply capacity
    float sFarm = 0;   // farming suitability 0..1 (grass-like cover, warm enough)
    // Technology state (see technology.h / Design/Technology.md):
    bool aware = false;      // knows farming exists
    bool practising = false; // farms; implies aware
    double practiceT = 0;    // sim day practice began (expertise grows from here)
    double nextTech = 1e18;  // next contact draw or resample moment
    bool techFires = false;  // whether nextTech is a real transition
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
    bool aware = false, practising = false;
    double practiceT = 0;
};

struct Field {
    std::vector<float> K;          // carrying capacity per cell (people), 0 on water
    std::vector<int> settlementAt; // settlement index per cell, -1 none
    std::vector<Settlement> settlements;
    std::vector<std::vector<int>> neighbours; // settlements within contact range
    std::vector<Band> bands;
    uint32_t nextBandId = 1;
    // Per-cell local properties, kept for founding settlements at runtime:
    std::vector<float> kFoodPMap, kWaterMap, sFarmMap;
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

inline Field build(const terrain::ContinentParams& cp, float seaLevel, const float rot[9],
                   terrain::V3 offset, const plates::Field& pf, const hydrology::Result& hy,
                   const atmosphere::Climatology* clim = nullptr) {
    Field f;
    f.K.assign(W * H, 0.0f);
    f.settlementAt.assign(W * H, -1);
    const std::vector<float>& hm = hy.heightM;

    // Everything the population model needs to know about one cell.
    auto evalCell = [&](int x, int y, float& kFoodP, float& kWater, float& sFarm) {
        int i = y * W + x;
        kFoodP = kWater = sFarm = 0;
        float h = hm[i];
        if (h <= 0) return;                                           // land only
        if (hy.cells[i].lakeLevel > hydrology::NO_LAKE + 1 && h < hy.cells[i].lakeLevel) return;

        hydrology::V3orig cd = hydrology::cellDir(x, y);
        terrain::V3 n = {cd.x, cd.y, cd.z};
        terrain::V3 w = terrain::rotate(rot, n) + offset;
        float lat = std::asin(std::clamp(n.z, -1.0f, 1.0f));
        float temp = terrain::temperatureC(lat, h);
        float moist = terrain::moistureAt(w, lat);
        // Coarse slope from neighbouring cell heights.
        float hx = hm[y * W + hydrology::wrapX(x + 1)] - hm[y * W + hydrology::wrapX(x - 1)];
        float hyv = hm[std::min(y + 1, H - 1) * W + x] - hm[std::max(y - 1, 0) * W + x];
        float cellKm = 2 * 3.14159265f * 6371.0f / W * std::max(std::cos(lat), 0.05f);
        float slope = std::sqrt(hx * hx + hyv * hyv) / (2000.0f * cellKm);
        float uplift = pf.sample({n.x, n.y, n.z}).uplift;
        float swamp = clim ? atmosphere::swampinessAt(*clim, lat, std::atan2(n.y, n.x)) : 0.0f;
        terrain::Mixture m = terrain::mixtureAt(h, slope, temp, moist, uplift,
                                                hy.cells[i].nearRiver > 0.5f, terrain::patchNoise(w), swamp);

        // kFood is sustained yield; the pristine ceiling is higher. Water is
        // a physical daily supply and is not scaled (it rarely binds before
        // farming and irrigation).
        kFoodP = coverYield(m) * FORAGE_KM2 / SUSTAIN_R;
        // Water within reach: accKm2 is runoff-equivalent drainage area at the
        // reference runoff (hydrology::reweight), fed by the climate's rain.
        float litresPerDay = hy.accKm2[i] * hydrology::REF_RUNOFF_MM_YR * 1.0e6f / 365.0f;
        kWater = litresPerDay * USABLE_WATER / WATER_L_PER_PERSON;
        sFarm = farmSuitability(m, temp);
    };

    f.kFoodPMap.assign(W * H, 0.0f);
    f.kWaterMap.assign(W * H, 0.0f);
    f.sFarmMap.assign(W * H, 0.0f);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            evalCell(x, y, f.kFoodPMap[i], f.kWaterMap[i], f.sFarmMap[i]);
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
        s.S = 0.5f * CAP_DAYS_SETTLED * s.P;
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
// overshoot at year ~33 and settling at the sustained capacity.
inline void derivatives(float P, float R, float S, float K, float& dP, float& dR, float& dS) {
    float flow = K * R;                                   // rations/day offered
    float H = std::min(flow, GATHER_SETTLED * P);         // limited by land and time
    float cap = CAP_DAYS_SETTLED * std::max(P, 1.0f);
    float fill = std::clamp(S / cap, 0.0f, 1.0f);
    float excl = std::clamp(1.0f - fill / HOARD_FILL, 0.0f, 1.0f);
    float shortfall = P > 0 ? std::clamp(1.0f - H / P, 0.0f, 1.0f) : 0.0f;
    float phi = P > 1 ? flow / P : 2.0f;
    float g = phi >= 1 ? GROWTH_MAX / 365.0f * std::min((phi - 1) / 0.11f, 1.0f) : 0.0f;
    dP = P * g - STARVE_MAX * P * excl * shortfall;
    dR = (1 - R) / (R_REGEN_YEARS * 365) - (P / std::max(K, 1.0f)) * R / (R_DEPLETE_YEARS * 365);
    dS = H - P;
}

// Integrate a settlement from its valid time to `now` and schedule the next
// re-evaluation at the moment its state will have drifted about 5%. Famine
// can move at percent-per-day, so steps stay short and the horizon also
// watches for the store crossing the hoarding threshold.
inline bool advance(Settlement& s, float K, double now) {
    if (K <= 0) { s.t = now; s.nextUpdate = now + 3650; return false; }
    float P = s.P, R = s.R, S = s.S;
    double span = now - s.t;
    int steps = std::clamp((int)(span / 5.0) + 1, 1, 500);
    float hstep = (float)(span / steps);
    for (int k = 0; k < steps && hstep > 0; k++) {
        float dP, dR, dS;
        derivatives(P, R, S, K, dP, dR, dS);
        P = std::max(P + dP * hstep, 0.0f);
        R = std::clamp(R + dR * hstep, 0.0f, 1.0f);
        S = std::clamp(S + dS * hstep, 0.0f, CAP_DAYS_SETTLED * std::max(P, 1.0f));
    }
    bool changed = std::fabs(P - s.P) > 0.5f || std::fabs(R - s.R) > 0.002f;
    s.P = P;
    s.R = R;
    s.S = S;
    s.t = now;
    float dP, dR, dS;
    derivatives(s.P, s.R, s.S, K, dP, dR, dS);
    double horizon = 1800;
    if (std::fabs(dP) > 1e-9) horizon = std::min(horizon, 0.05 * std::max(s.P, 50.0f) / std::fabs(dP));
    if (std::fabs(dR) > 1e-9) horizon = std::min(horizon, 0.05 * std::max(s.R, 0.1f) / std::fabs(dR));
    if (dS < -1e-9) {
        double toHoard = (s.S - HOARD_FILL * CAP_DAYS_SETTLED * s.P) / -dS;
        if (toHoard > 0) horizon = std::min(horizon, std::max(toHoard, 15.0));
    }
    s.nextUpdate = now + std::max(horizon, 5.0);
    return changed;
}

} // namespace population
