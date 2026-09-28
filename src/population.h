// The population model of Design/Population.md as arithmetic: the carrying
// capacity the terrain offers, computed once for every cell by build, then
// each settlement's state -- people, land condition, stores -- integrated
// against it by advance, at scheduled re-evaluations rather than ticks. The
// data these functions work on is settlement.h; the decisions taken between
// wakes (claims, journeys, raids) are the sim headers.
#pragma once
#include "settlement.h"
#include "daylight.h"
#include <cstdio>
#include <cstdlib>

namespace population {

// Cold water carries more than warm, and ice-locked water carries nothing
// anyone can reach for months.
inline float fishWaterFactor(float tempC) {
    if (tempC < -2.0f) return 0.35f;
    return std::clamp(1.25f - tempC / 26.0f, 0.35f, 1.25f);
}

// Natural food yield, people per km^2 at land condition R = 1, by cover class:
// bare, tundra, taiga, forest, rainforest, grass, steppe, savanna, shrub, marsh, desert.
constexpr float COVER_YIELD[terrain::NCOV] = {0.0f, 0.08f, 0.4f,  1.2f, 1.0f, 0.8f,
                                              0.3f, 0.6f,  0.2f, 1.0f, 0.02f};
// The share of each cover's yield that is wild game rather than gatherable
// plants: grass and tundra are only edible through the animals that eat
// them; forests feed people directly as well.
constexpr float GAME_SHARE[terrain::NCOV] = {0.0f, 0.9f, 0.6f,  0.4f, 0.25f, 0.7f,
                                             0.85f, 0.7f, 0.5f, 0.4f, 0.5f};

// Of a cover's animal food, the share that is small game. Herd country
// (tundra, steppe, grass) is dominated by the big animals in the regional
// pool; forest, marsh and rainforest hold more of what a bow is for -- so
// a megafauna collapse guts the steppe and leaves the woods a fallback.
constexpr float SMALL_SHARE[terrain::NCOV] = {0.0f, 0.15f, 0.30f, 0.50f, 0.60f, 0.25f,
                                              0.15f, 0.25f, 0.40f, 0.60f, 0.50f};

inline float coverYield(const terrain::Mixture& m) {
    float d = 0;
    for (int i = 0; i < terrain::NCOV; i++) d += m.cov[i] * COVER_YIELD[i];
    return d;
}

// The big-game part of the same yield: what the regional pool holds.
inline float coverGameYield(const terrain::Mixture& m) {
    float d = 0;
    for (int i = 0; i < terrain::NCOV; i++)
        d += m.cov[i] * COVER_YIELD[i] * GAME_SHARE[i] * (1.0f - SMALL_SHARE[i]);
    return d;
}

// The small-game part: local, quick to recover, and hard to catch unarmed.
inline float coverSmallYield(const terrain::Mixture& m) {
    float d = 0;
    for (int i = 0; i < terrain::NCOV; i++)
        d += m.cov[i] * COVER_YIELD[i] * GAME_SHARE[i] * SMALL_SHARE[i];
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

// Wood cover: the share of the surroundings a fuel gatherer finds trees on.
// Distinct from buildMat, which credits bare rock (stone builds a granary;
// it does not burn). Savanna and marsh carry scattered timber; tundra a
// little scrub and driftwood, which is why the far north heats with herds
// or not at all.
inline float woodSuitability(const terrain::Mixture& m) {
    return std::min(m.cov[2] + m.cov[3] + m.cov[4] + 0.5f * m.cov[8] + 0.3f * m.cov[7] +
                        0.25f * m.cov[9] + 0.05f * m.cov[1],
                    1.0f);
}

inline Field build(const terrain::ContinentParams& cp, float seaLevel, const float rot[9],
                   terrain::V3 offset, const plates::Field& pf, const hydrology::Result& hy,
                   const atmosphere::Climatology* clim = nullptr) {
    Field f;
    f.K.assign(W * H, 0.0f);
    f.settlementAt.assign(W * H, -1);
    const std::vector<float>& hm = hy.heightM;

    // Everything the population model needs to know about one cell.
    auto evalCell = [&](int x, int y, float& kFoodP, float& kWater, float& sFarm, float& pasture,
                        float& buildMat, float& kGame, float& kSmall, float& kFish, float& sFish,
                        float& sWood) {
        int i = y * W + x;
        kFoodP = kWater = sFarm = pasture = buildMat = kGame = kSmall = sWood = 0;
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
                                                dc.swamp, dc.tCold, dc.tWarm);

        // kFood is sustained yield; the pristine ceiling is higher. Water is
        // a physical daily supply and is not scaled (it rarely binds before
        // farming and irrigation).
        kFoodP = coverYield(m) * FORAGE_KM2 / SUSTAIN_R;
        kGame = coverGameYield(m) * FORAGE_KM2 / SUSTAIN_R;
        kSmall = coverSmallYield(m) * FORAGE_KM2 / SUSTAIN_R;
        // Water within reach: accKm2 is runoff-equivalent drainage area at the
        // reference runoff (hydrology::reweight), fed by the climate's rain.
        float litresPerDay = hy.accKm2[i] * hydrology::REF_RUNOFF_MM_YR * 1.0e6f / 365.0f;
        kWater = litresPerDay * USABLE_WATER / WATER_L_PER_PERSON;
        sFarm = farmSuitability(m, temp);
        pasture = pastureSuitability(m);
        // Building materials within reach: standing timber, else bare rock.
        // Never zero on land -- driftwood and fieldstone exist everywhere,
        // just slowly.
        buildMat = std::clamp(m.cov[2] + m.cov[3] + m.cov[4] + 0.3f * m.cov[8] +
                                  0.6f * m.cov[0],
                              0.15f, 1.0f);
        sWood = woodSuitability(m);

        // What the water in reach is worth. A shoreline is what makes it:
        // the sea at the door, a lake, or a river big enough to weir. Only
        // the best of the three counts -- a coast and a river mouth is a
        // better place to live, not two places.
        float shore = 0;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int yy = std::clamp(y + dy, 0, H - 1);
                int j = yy * W + hydrology::wrapX(x + dx);
                if (hm[j] <= 0) shore = std::max(shore, FISH_SEA_KM2);
                if (hy.cells[j].lakeLevel > hydrology::NO_LAKE + 1.0f)
                    shore = std::max(shore, FISH_LAKE_KM2);
            }
        float river = std::min(hy.accKm2[i] / FISH_RIVER_FULL, 1.0f);
        shore = std::max(shore, FISH_RIVER_KM2 * river);
        float perKm2 = shore * fishWaterFactor(temp);
        kFish = perKm2 * FORAGE_KM2 / SUSTAIN_R;
        sFish = std::clamp(perKm2 / FISH_SUIT_FULL, 0.0f, 1.0f);
    };

    f.kFoodPMap.assign(W * H, 0.0f);
    f.kWaterMap.assign(W * H, 0.0f);
    f.sFarmMap.assign(W * H, 0.0f);
    f.pastureMap.assign(W * H, 0.0f);
    f.buildMatMap.assign(W * H, 0.0f);
    f.kGameMap.assign(W * H, 0.0f);
    f.kSmallMap.assign(W * H, 0.0f);
    f.kFishMap.assign(W * H, 0.0f);
    f.sFishMap.assign(W * H, 0.0f);
    f.sWoodMap.assign(W * H, 0.0f);
#pragma omp parallel for
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            evalCell(x, y, f.kFoodPMap[i], f.kWaterMap[i], f.sFarmMap[i], f.pastureMap[i],
                     f.buildMatMap[i], f.kGameMap[i], f.kSmallMap[i], f.kFishMap[i],
                     f.sFishMap[i], f.sWoodMap[i]);
            // What an unskilled newcomer would find here: the land, plus the
            // fish anyone can take from a bank without gear.
            f.K[i] = std::min(f.kFoodPMap[i] + f.kFishMap[i] * FISH_BASE, f.kWaterMap[i]);
        }

    // Regional game pools: each region's sustainable draw is its game yield
    // summed over its area, times the accessible share. Pools start pristine.
    f.gameG.assign(atmosphere::W * atmosphere::H, 1.0f);
    f.gameDmax.assign(atmosphere::W * atmosphere::H, 0.0f);
    for (int y = 0; y < H; y++) {
        float lat = ((y + 0.5f) / H - 0.5f) * 3.14159265f;
        float cellKm2 = (2 * 3.14159265f * 6371.0f / W * std::max(std::cos(lat), 0.01f)) *
                        (3.14159265f * 6371.0f / H);
        for (int x = 0; x < W; x++) {
            int i = y * W + x;
            if (f.kGameMap[i] <= 0) continue;
            float density = f.kGameMap[i] * SUSTAIN_R / FORAGE_KM2; // people/km2 sustained
            f.gameDmax[gameRegion(i)] += density * cellKm2 * GAME_ACCESS;
        }
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
        float P0 = c.k * SUSTAIN_R * 0.5f; // half capacity: room to overshoot
        Settlement s = newSettlement(c.cell, seedCohorts(P0), P0, 1.0f, 0.0);
        s.id = f.nextSettlementId++;
        s.kFoodP = f.kFoodPMap[c.cell];
        s.kGame = f.kGameMap[c.cell];
        s.kSmall = f.kSmallMap[c.cell];
        s.kFish = f.kFishMap[c.cell];
        s.sFish = f.sFishMap[c.cell];
        s.gRegion = gameRegion(c.cell);
        s.kWater = f.kWaterMap[c.cell];
        s.sFarm = f.sFarmMap[c.cell];
        s.pasture = f.pastureMap[c.cell];
        s.buildMat = f.buildMatMap[c.cell];
        s.sWood = f.sWoodMap[c.cell];
        s.S = storageCapDays(s.P, s.granaries) * s.P; // the world opens on full stores
        s.fuelS = 0.5f * FUEL_CAP_KG * s.P;           // and half a woodpile
        // The world opens with each settlement holding what it needs, capped
        // at half the seeding distance so no two claims start overlapping.
        // The yields above are for the old fixed catchment, so they are
        // rescaled to the claim actually held.
        {
            float perKm2 = f.kFoodPMap[c.cell] / FORAGE_KM2 * SUSTAIN_R;
            float want = perKm2 > 0 ? claimRadiusFor(s.P / perKm2 * CLAIM_MARGIN) : CLAIM_FLOOR_KM;
            float take = std::clamp(want, CLAIM_FLOOR_KM, 40.0f);
            for (int k = 0; k < CLAIM_SECTORS; k++) s.claim[k] = take;
            s.claimKm2 = claimYieldKm2(take);
            float sc = s.claimKm2 / FORAGE_KM2;
            s.kFoodP *= sc;
            s.kGame *= sc;
            s.kSmall *= sc;
            s.kFish *= sc;
            s.kWater *= sc;
        }
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
// with built granaries (storageCapDays).
inline void derivatives(float P, float R, float S, float flow, float K, float capDays,
                        float gather, float& dP, float& dR, float& dS, float& dStarve) {
    float H = std::min(flow, gather);                     // limited by land and time
    float cap = capDays * std::max(P, 1.0f);
    float fill = std::clamp(S / cap, 0.0f, 1.0f);
    float excl = std::clamp(1.0f - fill / HOARD_FILL, 0.0f, 1.0f);
    float shortfall = P > 0 ? std::clamp(1.0f - H / P, 0.0f, 1.0f) : 0.0f;
    float phi = P > 1 ? flow / P : 2.0f;
    float g = phi >= 1 ? GROWTH_MAX / 365.0f * std::min((phi - 1) / 0.11f, 1.0f) : 0.0f;
    dStarve = STARVE_MAX * P * excl * shortfall; // deaths/day, the felt part
    dP = P * g - dStarve;
    (void)g;
    dR = (1 - R) / (R_REGEN_YEARS * 365) - (P / std::max(K, 1.0f)) * R / (R_DEPLETE_YEARS * 365);
    dS = H - P;
}

// One day of demography, in place. Births come from the women and are
// multiplied by food; children take fifteen years to become adults and
// many die first; adults age into a short old age. Famine deaths are
// handed out by vulnerability -- the young and the old go first -- which
// is why a hungry settlement loses its next generation before its
// workers. No share is enforced anywhere: the structure is what the flows
// leave behind.
inline void stepCohorts(Cohorts& c, float phi, float starveDeaths, float dt) {
    float yr = dt / 365.0f;
    float surplus = phi >= 1 ? std::min((phi - 1.0f) / 0.11f, 1.0f) : 0.0f;
    float births = BIRTHS_REPLACE * (1.0f + FERT_SURPLUS * surplus) * c.W * yr;
    float grow = c.C / CHILD_YEARS * yr;      // reaching adulthood
    float dieC = MORT_CHILD * c.C * yr;
    float ageM = c.M / ADULT_YEARS * yr, ageW = c.W / ADULT_YEARS * yr;
    float dieM = MORT_ADULT * c.M * yr, dieW = MORT_ADULT * c.W * yr;
    float dieE = MORT_ELDER * c.E * yr;
    // Famine, weighted by who survives it worst.
    float wsum = FAMINE_W_CHILD * c.C + FAMINE_W_ADULT * (c.M + c.W) + FAMINE_W_ELDER * c.E;
    float f = wsum > 1e-6f ? starveDeaths * dt / wsum : 0.0f;
    c.C = std::max(c.C + births - grow - dieC - f * FAMINE_W_CHILD * c.C, 0.0f);
    c.M = std::max(c.M + BOY_SHARE * grow - ageM - dieM - f * FAMINE_W_ADULT * c.M, 0.0f);
    c.W = std::max(c.W + (1.0f - BOY_SHARE) * grow - ageW - dieW - f * FAMINE_W_ADULT * c.W,
                   0.0f);
    c.E = std::max(c.E + ageM + ageW - dieE - f * FAMINE_W_ELDER * c.E, 0.0f);
}

// A band on the road: no births, but famine still takes the weak first.
inline void bandStarve(Cohorts& c, float deaths) {
    float wsum = FAMINE_W_CHILD * c.C + FAMINE_W_ADULT * (c.M + c.W) + FAMINE_W_ELDER * c.E;
    if (wsum <= 1e-6f) return;
    float f = deaths / wsum;
    c.C = std::max(c.C - f * FAMINE_W_CHILD * c.C, 0.0f);
    c.M = std::max(c.M - f * FAMINE_W_ADULT * c.M, 0.0f);
    c.W = std::max(c.W - f * FAMINE_W_ADULT * c.W, 0.0f);
    c.E = std::max(c.E - f * FAMINE_W_ELDER * c.E, 0.0f);
}

// What a settlement eats, term by term: the diet, defined once. Every
// place that needs "how much food is here" -- the annual capacity
// (effectiveFood, technology::effectiveK), the seasonal flow (foodFlow),
// the wake horizon (scheduleWake), the game pools' draw (sim::gameTick) --
// assembles its answer from these terms with its own seasonal factors, so
// a change to what counts as food is made in foodTerms alone. All terms
// are annual-mean, per day, before the land condition R.
struct FoodTerms {
    float plant = 0;     // gatherable plants: the yield less its game share
    float bigGame = 0;   // the regional pool's herds, at their present health
    float smallGame = 0; // the local small game, at this group's bows
    float farm = 0;      // the standing plots at current expertise
    float herd = 0;      // the livestock's mean flow (seasonal mean ~0.85)
    float farmyard = 0;  // household animals, no pasture needed
    float fish = 0;      // the water in reach, at current gear
};

inline FoodTerms foodTerms(const Settlement& s, const SeasonCtx& ctx) {
    FoodTerms f;
    f.plant = s.kFoodP - s.kGame - s.kSmall;
    float bigEff = huntEff(ctx.gameG) * (1.0f + BOW_BIG_GAIN * ctx.bowCover * ctx.archExp);
    f.bigGame = s.kGame * bigEff;
    f.smallGame = s.kSmall * smallGameEff(ctx.bowCover, ctx.archExp);
    f.farm = ctx.farmFlow;
    f.herd = s.herd * 0.85f;
    f.farmyard = FARMYARD_SHARE_POP * s.kFoodP * ctx.husbExp;
    f.fish = s.kFish * fishEff(ctx.fishExp);
    return f;
}

// Annual food capacity: foraging scaled by the seasonal mean (with the
// game-borne share tracking the regional pool's health), farming's
// harvest-shaped total, the herd's current flow and the farmyard bonus,
// the fish; water caps the whole. Before the land condition: the caller
// multiplies by R where the land is worn (fish carry no such term, but
// R is applied to the whole by every caller that applies it at all).
inline float effectiveFood(const Settlement& s, const SeasonCtx& ctx) {
    FoodTerms f = foodTerms(s, ctx);
    return std::min((f.plant + f.bigGame + f.smallGame) * s.meanF + f.farm + (f.herd + f.farmyard) +
                        f.fish,
                    s.kWater);
}

// The seasonal food flow at time t: foraging follows the forage factor,
// farming follows squared growing activity normalized to keep its annual
// total (a prominent harvest season; year-round cropping in the tropics),
// and water caps the whole.
inline float foodFlow(const Settlement& s, const SeasonCtx& ctx, float R, double t) {
    FoodTerms f = foodTerms(s, ctx);
    // Three kinds of food from the land: plants, the herds of the regional
    // pool, and the small game a bow is for.
    float hunted = (f.bigGame + f.smallGame) * affinityBonus(ctx.aff.hunt);
    float forage = f.plant * affinityBonus(ctx.aff.gather) + hunted;
    float farm = f.farm; // the standing plots, at current expertise
    float fF = s.meanF, fG2 = 1.0f, fFish = 1.0f;
    if (ctx.clim) {
        float tC = cachedSeasonT(s, t);
        fF = atmosphere::forageFactor(tC);
        float g = atmosphere::growthActivity(tC);
        fG2 = g * g / std::max(s.meanG2, 0.05f);
        // The run: fish come in a season and are thin outside it. Not as
        // sharp as a harvest, sharp enough to be worth drying.
        fFish = FISH_WINTER + (1.0f - FISH_WINTER) * g;
    }
    // Husbandry: the herd is a walking store -- its flow barely dips in
    // winter (fodder and slaughter). Plus the pasture-free farmyard animals.
    float gNow = std::clamp((fF - 0.12f) / 0.88f, 0.0f, 1.0f);
    float husb = (s.herd * (0.7f + 0.3f * gNow) + f.farmyard) * R * affinityBonus(ctx.aff.herd);
    // Fish are not scaled by R: a coast is not worn out by being fished,
    // which is the whole reason a fishing people can stay put.
    float fish = f.fish * fFish * affinityBonus(ctx.aff.hunt);
    return std::min((forage * fF + farm * fG2 * affinityBonus(ctx.aff.farm)) * R + husb + fish,
                    s.kWater);
}

// What advance() integrates, carried through one call: copied out of the
// settlement at the start (loadStep), stepped in place by the functions
// below, written back at the end (storeStep). The day's own values ride
// along so each step function takes this and the settlement, not a dozen
// scalars.
struct Step {
    // Carried across the sub-steps:
    Cohorts pop;
    float P = 0, R = 0, S = 0;
    float granaries = 0, buildWork = 0;
    float fillLo = 0, fillHi = 0, granNeed = 0;
    float starved = 0, bows = 0;
    float fuelS = 0, coldYr = 0, labFuel = 0, labProj = 0;
    float fuelNet = 0; // kg/day the pile last gained or lost (horizon watch)
    float farmsteads = 0, fsteadWork = 0, tillWork = 0;
    int tillSite = -1;
    float sumTilled = 0;
    double cycleT = 0;
    // This sub-step:
    double tk = 0;     // its midpoint, sim day
    float hstep = 0;   // its length, days
    float capDays = 0; // storage cap at its start
    float flow = 0;    // the land's food flow at tk, rations/day
    float phiNow = 0;  // flow per head
    float wh = 0;      // the work day, hours
    float fill = 0;    // store fill at its end, 0..1
    // Its labour ledger, man-days a day (Design/Resources.md): the budget,
    // then each level of the priority stack's claim on it in order.
    float budgetMD = 0;  // the day's budget
    float foodMD = 0;    // food: the harvest eaten or banked
    float fuelMD = 0;    // heat: the dedicated cutters
    float projMD = 0;    // the projects: builds and crafts, as drawn
    float projScale = 0; // what the projects get of their ceilings, 0..1
};

// Measurement hook (test_resources.cpp): when set, advance() reports every
// sub-step's ledger here, so a probe can check that the day's labour is
// never overspent. Not a game setting; the simulation runs serially, so no
// lock.
struct LedgerAudit {
    double maxRatio = 0;     // (food + heat + projects) / budget, worst sub-step
    double overDays = 0;     // settlement-days spent over the budget
    double famineDays = 0;   // settlement-days projects drew labour at or below HOARD_FILL
    long long overSteps = 0; // the same two, counted in sub-steps
    long long famineSteps = 0;
};
inline LedgerAudit* LEDGER_AUDIT = nullptr;

inline Step loadStep(const Settlement& s) {
    Step st;
    st.pop = s.pop;
    st.P = st.pop.total();
    st.R = s.R;
    st.S = s.S;
    st.granaries = s.granaries;
    st.buildWork = s.buildWork;
    st.fillLo = s.fillLo;
    st.fillHi = s.fillHi;
    st.granNeed = s.granNeedYrs;
    st.starved = s.starvedYr;
    st.bows = s.bows;
    st.fuelS = s.fuelS;
    st.coldYr = s.coldYr;
    st.labFuel = s.labFuel;
    st.labProj = s.labProj;
    st.farmsteads = s.farmsteads;
    st.fsteadWork = s.fsteadWork;
    st.tillWork = s.tillWork;
    st.tillSite = s.tillSite;
    for (int i = 0; i <= FSTEAD_MAX; i++) st.sumTilled += s.tilled[i];
    st.cycleT = s.cycleT;
    return st;
}

inline void storeStep(Settlement& s, const Step& st) {
    s.pop = st.pop;
    s.P = st.P;
    s.R = st.R;
    s.S = st.S;
    s.granaries = st.granaries;
    s.buildWork = st.buildWork;
    s.fillLo = st.fillLo;
    s.fillHi = st.fillHi;
    s.granNeedYrs = st.granNeed;
    s.starvedYr = st.starved;
    s.bows = st.bows;
    s.fuelS = st.fuelS;
    s.coldYr = st.coldYr;
    s.labFuel = st.labFuel;
    s.labProj = st.labProj;
    s.farmsteads = st.farmsteads;
    s.fsteadWork = st.fsteadWork;
    s.tillWork = st.tillWork;
    s.tillSite = (int8_t)st.tillSite;
    s.cycleT = st.cycleT;
}

// The labour ledger opens (Design/Resources.md): the day's budget of
// man-days, and food's claim on it, the first in the priority stack. The
// claim is the harvest actually eaten or banked, not the notional maximum:
// a full larder frees hands.
inline void stepFoodLabour(Step& st) {
    st.budgetMD = st.P * st.wh / 12.0f;
    float capS = st.capDays * std::max(st.P, 1.0f);
    float Hfood = std::min(st.flow, GATHER_SETTLED * st.budgetMD);
    float useful = std::min(Hfood, st.P + std::max(capS - st.S, 0.0f) / std::max(st.hstep, 1.0f));
    st.foodMD = useful / GATHER_SETTLED;
    st.fuelMD = 0;
}

// Heat and the labour ledger (Design/Resources.md). Warmth is a demand like
// food's, and burning wood only its leading mode: the herd's dung burns
// too, and ordinary rounds sweep up deadfall (the byproduct) which covers
// the cooking fire wherever there are woods at all. Only the need past that
// pulls dedicated cutters out of the day's budget -- whatever the food work
// leaves free, and nothing while hunger has the stores down to hoarding:
// famine pre-empts the woods. The pile fills in the mild seasons and drains
// in winter; when it runs dry in the cold, the hands famine would take
// first are taken by the cold instead. Returns the cold's deaths per day.
inline float stepHeat(const Settlement& s, Step& st) {
    if (!HEAT_ENABLED) return 0.0f;
    float needKg = fuelNeedKg(cachedSeasonT(s, st.tk)) * st.P;
    float budget = st.budgetMD; // stepFoodLabour opened the day's ledger
    float capS = st.capDays * std::max(st.P, 1.0f);
    float freeMD = std::max(budget - st.foodMD, 0.0f);
    float fillNow = std::clamp(st.S / capS, 0.0f, 1.0f);
    float dung = s.herd * DUNG_KG_PER_FED;
    float byp = st.P * WOOD_BYPRODUCT_KG * s.sWood;
    float capKg = FUEL_CAP_KG * std::max(st.P, 1.0f);
    float gatherKg = WOOD_GATHER_KG * s.sWood; // per man-day of cutting
    float wantKg =
        std::max(needKg - dung - byp, 0.0f) + std::max(capKg - st.fuelS, 0.0f) / FUEL_PILE_DAYS;
    float cutMD = gatherKg > 0 && fillNow > HOARD_FILL ? std::min(wantKg / gatherKg, freeMD) : 0.0f;
    st.fuelMD = cutMD;
    st.labFuel = budget > 0 ? cutMD / budget : 0.0f;
    float inflow = dung + byp + cutMD * gatherKg;
    // The hearth burns around the clock; no daylight factor here.
    float burn = std::min(needKg * st.hstep, st.fuelS + inflow * st.hstep);
    float unmet = needKg > 0 ? 1.0f - burn / (needKg * st.hstep) : 0.0f;
    float dCold = COLD_MAX * st.P * unmet * unmet;
    st.fuelS = std::clamp(st.fuelS + inflow * st.hstep - burn, 0.0f, capKg);
    st.fuelNet = inflow - needKg;
    return dCold;
}

// The annual fill cycle: track the store-fill extremes and judge granary
// demand once a year (see the constants in settlement.h). The signal also
// feeds need-driven invention: consecutive binding years make an unaware
// settlement desperate enough to invent (technology.h). Field demand is
// measured yearly the same way: a farming people whose food binds selects
// the next plot and clears it, as long as there are hands to work what
// stands (the 8 ha a person can tend). Where the sites in hand are all
// tilled up, the next order is a FARMSTEAD instead: move a household out
// and open a new block (sim::updateFarmland decides where the next plot
// would go and whether the next slot is worth it).
inline void stepFillCycle(const Settlement& s, const SeasonCtx& ctx, Step& st) {
    st.fillLo = std::min(st.fillLo, st.fill);
    st.fillHi = std::max(st.fillHi, st.fill);
    if (!(st.tk - st.cycleT >= 365.0)) return;
    bool binds = st.fillHi > GRANARY_HI && st.fillLo < GRANARY_LO;
    st.granNeed = binds ? st.granNeed + 1.0f : 0.0f;
    if (binds && st.buildWork <= 0 && ctx.granExp > 0) st.buildWork = GRANARY_WORK;
    bool wantsLand =
        ctx.farmExp > 0 && needRamp(st.phiNow) > 0.1f && st.sumTilled < st.P * FARM_KM2_PER_PERSON;
    if (wantsLand && st.tillWork <= 0 && s.tillSiteNext >= 0) {
        st.tillSite = s.tillSiteNext;
        st.tillWork = PLOT_KM2 * TILL_WORK_PER_KM2;
    }
    if (wantsLand && s.tillSiteNext < 0 && st.fsteadWork <= 0 && st.farmsteads < s.fsteadMax &&
        s.fsteadNextOk)
        st.fsteadWork = FSTEAD_WORK;
    st.cycleT = st.tk;
    st.fillLo = st.fillHi = st.fill;
}

// One build clock: `work` man-days left, paid down at `rate` man-days a
// day. True on the sub-step it reaches zero; the work total itself never
// changes, only the pace.
inline bool workClock(float& work, float rate, float hstep) {
    if (work <= 0) return false;
    work -= rate * hstep;
    if (work > 0) return false;
    work = 0;
    return true;
}

// The last level of the priority stack (Design/Resources.md, "Sharing the
// surplus among projects"): the four projects -- a granary, a farmstead, a
// plot being cleared, bows -- share what food and heat left of the day's
// budget. Each asks its ceiling, a share of the people; when the surplus
// covers them all each gets its ceiling, and when it does not every one is
// scaled by the same fraction. A settlement at the hoarding threshold has
// no surplus, so nothing is built or carved. The conditions here are the
// ones stepBuilding and stepBows work under.
inline void allocateProjects(const SeasonCtx& ctx, Step& st) {
    st.projMD = 0;
    st.projScale = 0;
    if (!(st.fill > HOARD_FILL)) return; // famine pre-empts every project
    float share = 0;
    if (ctx.granExp > 0 && st.buildWork > 0) share += GRANARY_LABOUR_SHARE;
    if (ctx.farmExp > 0 && st.fsteadWork > 0) share += FSTEAD_LABOUR_SHARE;
    if (ctx.farmExp > 0 && st.tillSite >= 0 && st.tillWork > 0) share += TILL_LABOUR_SHARE;
    if (ctx.archExp > 0 && st.bows < st.P * BOW_PER_HUNTER) share += BOW_LABOUR_SHARE;
    float wantMD = st.P * share;
    float surplusMD = std::max(st.budgetMD - st.foodMD - st.fuelMD, 0.0f);
    st.projScale = wantMD > surplusMD ? surplusMD / wantMD : 1.0f;
}

// One project's draw on the ledger: its ceiling scaled by what the surplus
// allows, booked in man-days a day. Nothing while the project stands idle.
inline float drawLabour(Step& st, bool active, float share) {
    if (!active) return 0.0f;
    float md = st.P * share * st.projScale;
    st.projMD += md;
    return md;
}

// The three things a settlement builds, each on the clock above: the hands
// come from the ledger's surplus (allocateProjects), expertise sets the
// pace. Granaries and farmsteads are timber and stone, so local materials
// set the gathering; a plot is cleared with fire and axes -- girdle, burn,
// stump, break -- so hands and skill alone set its pace.
inline void stepBuilding(Settlement& s, const SeasonCtx& ctx, Step& st) {
    if (!(st.fill > HOARD_FILL)) return; // famine pauses every build
    if (ctx.granExp > 0 &&
        workClock(st.buildWork,
                  drawLabour(st, st.buildWork > 0, GRANARY_LABOUR_SHARE) * ctx.granExp * s.buildMat,
                  st.hstep)) {
        st.granaries += 1;
        if (s.builtGranaries < 250) s.builtGranaries++;
    }
    if (ctx.farmExp > 0 &&
        workClock(st.fsteadWork,
                  drawLabour(st, st.fsteadWork > 0, FSTEAD_LABOUR_SHARE) * ctx.farmExp * s.buildMat,
                  st.hstep)) {
        st.farmsteads += 1;
        if (s.builtFsteads < 250) s.builtFsteads++;
    }
    if (st.tillSite >= 0 && ctx.farmExp > 0 &&
        workClock(st.tillWork,
                  drawLabour(st, st.tillWork > 0, TILL_LABOUR_SHARE) * (0.5f + 0.5f * ctx.farmExp),
                  st.hstep)) {
        float cap = st.tillSite == 0 ? VILLAGE_FIELDS_KM2 : FSTEAD_KM2;
        float& t = s.tilled[std::clamp(st.tillSite, 0, FSTEAD_MAX)];
        float add = std::min(PLOT_KM2, cap - t);
        t += std::max(add, 0.0f);
        st.sumTilled += std::max(add, 0.0f);
        st.tillSite = -1;
    }
}

// Bows: one bowyer finishes one bow in BOW_WORK_DAYS however large the
// settlement, so a crowd only carves more of them at once. They are made
// up to one per hunter and no further, and they wear out -- in famine too,
// when nobody carves (allocateProjects).
inline void stepBows(const Settlement& s, const SeasonCtx& ctx, Step& st) {
    if (!(ctx.archExp > 0)) return;
    float want = st.P * BOW_PER_HUNTER;
    float rate = drawLabour(st, st.bows < want, BOW_LABOUR_SHARE) * s.buildMat /
                 (BOW_WORK_DAYS / std::max(ctx.archExp, 0.2f));
    st.bows = std::max(st.bows + (rate - st.bows / BOW_LIFE_DAYS) * st.hstep, 0.0f);
}

// The ledger closes: the projects' share of the day is the readout beside
// the woodcutters', and the day is never overspent -- food's and heat's
// claims stay inside the budget by construction and the projects inside
// what those two leave, so an overdraw is a bug and stops the run.
inline void closeLedger(Step& st) {
    st.labProj = st.budgetMD > 0 ? st.projMD / st.budgetMD : 0.0f;
    float spentMD = st.foodMD + st.fuelMD + st.projMD;
    if (!(spentMD <= st.budgetMD * 1.001f + 1e-3f)) {
        fprintf(stderr, "labour ledger overdrawn: food %g + heat %g + projects %g of %g man-days\n",
                st.foodMD, st.fuelMD, st.projMD, st.budgetMD);
        abort();
    }
    if (!LEDGER_AUDIT || !(st.budgetMD > 0)) return;
    LedgerAudit& a = *LEDGER_AUDIT;
    double ratio = (double)spentMD / st.budgetMD;
    a.maxRatio = std::max(a.maxRatio, ratio);
    if (ratio > 1.0 + 1e-4) {
        a.overDays += st.hstep;
        a.overSteps++;
    }
    if (st.projMD > 0 && !(st.fill > HOARD_FILL)) {
        a.famineDays += st.hstep;
        a.famineSteps++;
    }
}

// The herd grows logistically toward what the pasture in the claim can
// carry, and is gone the day there is no pasture at all.
inline void stepHerd(Settlement& s, float herdCap, float hstep) {
    if (s.herd > 0 && herdCap > 0)
        s.herd = std::clamp(s.herd + HERD_GROWTH_YR / 365.0f * s.herd * (1.0f - s.herd / herdCap) *
                                         hstep,
                            0.0f, herdCap * 1.05f);
    else if (herdCap <= 0)
        s.herd = 0;
}

// What they have been living on pulls their affinities that way, over
// generations. Fighting is not fed from food; it comes from raiding and
// being raided, and fades in peace (raids.h).
inline void driftAffinity(Settlement& s, const SeasonCtx& ctx, double span) {
    float plant = (s.kFoodP - s.kGame - s.kSmall) * s.meanF;
    float game = (s.kGame + s.kSmall) * s.meanF;
    float crop = ctx.farmFlow;
    float stock = s.herd * 0.85f + FARMYARD_SHARE_POP * s.kFoodP * ctx.husbExp;
    float tot = std::max(plant + game + crop + stock, 1e-3f);
    float k = std::min((float)(span / (AFFINITY_TAU_YEARS * 365.0)), 1.0f);
    s.aff.gather += (plant / tot - s.aff.gather) * k;
    s.aff.hunt += (game / tot - s.aff.hunt) * k;
    s.aff.farm += (crop / tot - s.aff.farm) * k;
    s.aff.herd += (stock / tot - s.aff.herd) * k;
    s.aff.fight *= std::exp(-(float)(span / (FIGHT_FORGET_YEARS * 365.0)));
}

// Schedule the next re-evaluation at the moment the state will have drifted
// about 5%. The horizon comes from the ANNUAL-MEAN flow: the seasonal
// oscillation is recurring, so a settlement in seasonal equilibrium still
// sleeps long. Famine can move at percent-per-day, so the horizon also
// watches for the store crossing the hoarding threshold, and a draining
// woodpile is a deadline like a draining larder.
inline void scheduleWake(Settlement& s, float K, const SeasonCtx& ctx, float fuelNet, double now) {
    float capDays = storageCapDays(s.P, s.granaries);
    FoodTerms f = foodTerms(s, ctx);
    float forageBase = f.plant * affinityBonus(ctx.aff.gather) +
                       (f.bigGame + f.smallGame) * affinityBonus(ctx.aff.hunt);
    float meanFlow = std::min((forageBase * s.meanF + f.farm) * s.R, s.kWater);
    float dP, dR, dS, dStarveMean;
    derivatives(s.P, s.R, s.S, meanFlow, K, capDays, GATHER_SETTLED * s.P, dP, dR, dS, dStarveMean);
    double horizon = 1800;
    if (std::fabs(dP) > 1e-9)
        horizon = std::min(horizon, 0.05 * std::max(s.P, 50.0f) / std::fabs(dP));
    if (std::fabs(dR) > 1e-9)
        horizon = std::min(horizon, 0.05 * std::max(s.R, 0.1f) / std::fabs(dR));
    if (dS < -1e-9) {
        double toHoard = (s.S - HOARD_FILL * capDays * s.P) / -dS;
        if (toHoard > 0) horizon = std::min(horizon, std::max(toHoard, 15.0));
    }
    // Wake by the day the pile runs out (and keep waking while the hearths
    // stand cold -- the population horizon above cannot see the cold's
    // deaths).
    if (fuelNet < -1e-6f) horizon = std::min(horizon, std::max((double)(s.fuelS / -fuelNet), 15.0));
    s.nextUpdate = now + std::max(horizon, 5.0);
}

// Integrate a settlement from its valid time to `now`, in sub-steps of at
// most five days, then schedule the next re-evaluation (scheduleWake).
// Each sub-step: the day's food flow and work day, the derivatives, the
// labour ledger's food and heat claims, the hearth, the cohorts, the
// stores, then the annual judgement, the surplus shared among the builds
// and the bows, and the herd. Returns whether anything visible changed.
inline bool advance(Settlement& s, float K, const SeasonCtx& ctx, double now) {
    if (K <= 0) {
        s.t = now;
        s.nextUpdate = now + 3650;
        return false;
    }
    float herdCap =
        s.pasture * s.claimKm2 * HERD_PASTURE_K / SUSTAIN_R * (0.3f + 0.7f * ctx.husbExp);
    Step st = loadStep(s);
    float lat = std::asin(std::clamp(ctx.n.z, -1.0f, 1.0f));
    float lon = std::atan2(ctx.n.y, ctx.n.x);
    double span = now - s.t;
    int steps = std::clamp((int)(span / 5.0) + 1, 1, 800);
    st.hstep = (float)(span / steps);
    for (int k = 0; k < steps && st.hstep > 0; k++) {
        st.tk = s.t + (k + 0.5) * st.hstep;
        st.capDays = storageCapDays(st.P, st.granaries);
        st.flow = foodFlow(s, ctx, st.R, st.tk);
        // The work day (daylight.h): daylight up to the waking cap, plus a
        // firelight extension bought by hunger. The gather budget follows
        // the hours; the 1.5/day constant is the 12-hour baseline.
        st.phiNow = st.P > 1 ? st.flow / st.P : 2.0f;
        st.wh = daylight::workHours(lat, st.tk, needRamp(st.phiNow));
        float dP, dR, dS, dStarve;
        derivatives(st.P, st.R, st.S, st.flow, K, st.capDays, GATHER_SETTLED * st.P * st.wh / 12.0f,
                    dP, dR, dS, dStarve);
        stepFoodLabour(st);
        float dCold = stepHeat(s, st);
        st.starved += (dStarve + dCold) * st.hstep;
        st.starved *= std::max(1.0f - st.hstep / 365.0f, 0.0f); // trailing year
        st.coldYr += dCold * st.hstep;
        st.coldYr *= std::max(1.0f - st.hstep / 365.0f, 0.0f);
        // Sub-day steps see the rhythm: harvesting and eating happen inside
        // the day's activity window, so stores hold flat through the night.
        double a = s.t + k * (double)st.hstep;
        float act = st.hstep >= 1.0f
                        ? 1.0f
                        : (float)(daylight::activeDays(lon, a, a + st.hstep, st.wh) / st.hstep);
        stepCohorts(st.pop, st.phiNow, dStarve + dCold, st.hstep);
        st.P = st.pop.total();
        st.R = std::clamp(st.R + dR * st.hstep, 0.0f, 1.0f);
        float cap = st.capDays * std::max(st.P, 1.0f);
        st.S = std::clamp(st.S + dS * st.hstep * act, 0.0f, cap);
        st.fill = st.S / cap;
        stepFillCycle(s, ctx, st);
        allocateProjects(ctx, st);
        stepBuilding(s, ctx, st);
        stepBows(s, ctx, st);
        closeLedger(st);
        stepHerd(s, herdCap, st.hstep);
    }
    bool changed = std::fabs(st.P - s.P) > 0.5f || std::fabs(st.R - s.R) > 0.002f ||
                   st.granaries != s.granaries || st.farmsteads != s.farmsteads;
    driftAffinity(s, ctx, span);
    storeStep(s, st);
    s.t = now;
    scheduleWake(s, K, ctx, st.fuelNet, now);
    return changed;
}

} // namespace population
