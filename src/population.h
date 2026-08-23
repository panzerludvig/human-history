// Population: a carrying-capacity field derived from terrain and water, and
// settlements — sparse actors with people P and land condition R that evolve
// by scheduled re-evaluations, not ticks. See Design/Population.md.
#pragma once
#include "terrain.h"
#include "hydrology.h"
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
constexpr int MAX_SETTLEMENTS = 400;

struct Settlement {
    int cell;
    float P;           // people
    float R;           // land condition 0..1
    double t;          // sim day at which P and R are valid
    double nextUpdate; // sim day of the next scheduled re-evaluation
};

struct Field {
    std::vector<float> K;          // carrying capacity per cell (people), 0 on water
    std::vector<int> settlementAt; // settlement index per cell, -1 none
    std::vector<Settlement> settlements;
};

// Natural food yield, people per km^2 at land condition R = 1, by cover class:
// bare, tundra, taiga, forest, rainforest, grass, steppe, savanna, shrub, marsh, desert.
inline float coverYield(const terrain::Mixture& m) {
    static const float y[terrain::NCOV] = {0.0f, 0.08f, 0.4f, 1.2f, 1.0f, 0.8f, 0.3f, 0.6f, 0.2f, 1.0f, 0.02f};
    float d = 0;
    for (int i = 0; i < terrain::NCOV; i++) d += m.cov[i] * y[i];
    return d;
}

inline Field build(const terrain::ContinentParams& cp, float seaLevel, const float rot[9],
                   terrain::V3 offset, const plates::Field& pf, const hydrology::Result& hy) {
    Field f;
    f.K.assign(W * H, 0.0f);
    f.settlementAt.assign(W * H, -1);
    const std::vector<float>& hm = hy.heightM;

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            float h = hm[i];
            if (h <= 0) continue;                                     // land only
            if (hy.cells[i].lakeLevel > hydrology::NO_LAKE + 1 && h < hy.cells[i].lakeLevel) continue;

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
            terrain::Mixture m = terrain::mixtureAt(h, slope, temp, moist, uplift,
                                                    hy.cells[i].nearRiver > 0.5f, terrain::patchNoise(w));

            float kFood = coverYield(m) * FORAGE_KM2;
            // Water within reach: discharge from the drainage area through this
            // cell, with runoff set by the moisture field.
            float runoffMmYr = 20.0f + 580.0f * moist * moist;
            float litresPerDay = hy.accKm2[i] * runoffMmYr * 1.0e6f / 365.0f;
            float kWater = litresPerDay * USABLE_WATER / WATER_L_PER_PERSON;
            // kFood is sustained yield; the pristine ceiling is higher. Water
            // is a physical daily supply and is not scaled (it never binds yet).
            f.K[i] = std::min(kFood / SUSTAIN_R, kWater);
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
        f.settlements.push_back({c.cell, c.k * SUSTAIN_R * 0.5f, 1.0f, 0.0, 0.0});
    }
    // Startup listing for testing: where the first settlements are.
    for (int i = 0; i < (int)f.settlements.size() && i < 5; i++) {
        int cx = f.settlements[i].cell % W, cy = f.settlements[i].cell / W;
        float lat = ((cy + 0.5f) / H) * 180.0f - 90.0f, lon = ((cx + 0.5f) / W) * 360.0f - 180.0f;
        fprintf(stderr, "settlement %d: lat %.2f lon %.2f K %.0f\n", i, lat, lon, f.K[f.settlements[i].cell]);
    }
    return f;
}

inline void derivatives(float P, float R, float K, float& dP, float& dR) {
    float supply = K * R;
    float phi = P > 1 ? supply / P : 2.0f;
    float g = phi >= 1 ? GROWTH_MAX * std::min((phi - 1) / 0.11f, 1.0f)
                       : -DECLINE_MAX * std::min((1 - phi) / 0.22f, 1.0f);
    dP = P * g / 365.0f;
    dR = (1 - R) / (R_REGEN_YEARS * 365) - (P / std::max(K, 1.0f)) * R / (R_DEPLETE_YEARS * 365);
}

// Integrate a settlement from its valid time to `now` and schedule the next
// re-evaluation at the moment its state will have drifted about 5%.
inline bool advance(Settlement& s, float K, double now) {
    if (K <= 0) { s.t = now; s.nextUpdate = now + 3650; return false; }
    float P = s.P, R = s.R;
    double span = now - s.t;
    int steps = std::clamp((int)(span / 90.0), 1, 12);
    float hstep = (float)(span / steps);
    for (int k = 0; k < steps && hstep > 0; k++) {
        float d1P, d1R, d2P, d2R, d3P, d3R, d4P, d4R;
        derivatives(P, R, K, d1P, d1R);
        derivatives(P + d1P * hstep / 2, R + d1R * hstep / 2, K, d2P, d2R);
        derivatives(P + d2P * hstep / 2, R + d2R * hstep / 2, K, d3P, d3R);
        derivatives(P + d3P * hstep, R + d3R * hstep, K, d4P, d4R);
        P += hstep / 6 * (d1P + 2 * d2P + 2 * d3P + d4P);
        R += hstep / 6 * (d1R + 2 * d2R + 2 * d3R + d4R);
    }
    bool changed = std::fabs(P - s.P) > 0.5f || std::fabs(R - s.R) > 0.002f;
    s.P = std::max(P, 0.0f);
    s.R = std::clamp(R, 0.0f, 1.0f);
    s.t = now;
    float dP, dR;
    derivatives(s.P, s.R, K, dP, dR);
    double horizon = 1800;
    if (std::fabs(dP) > 1e-9) horizon = std::min(horizon, 0.05 * std::max(s.P, 50.0f) / std::fabs(dP));
    if (std::fabs(dR) > 1e-9) horizon = std::min(horizon, 0.05 * std::max(s.R, 0.1f) / std::fabs(dR));
    s.nextUpdate = now + std::max(horizon, 30.0);
    return changed;
}

} // namespace population
