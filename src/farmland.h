// The ground plan of a settlement: the golden-angle spiral on which every
// built thing -- houses, granaries, farmsteads, plots -- stands, defined
// once here, mirrored exactly in shaders/globe.frag so the map draws what
// the simulation enforces. The fields are priced where they stand, so what the
// standing plots feed (Settlement::farmK) is computed here as well.
// Design/Technology.md (farming, farmsteads, tilled land).
#pragma once
#include "constants.h"
#include "claims.h"
#include <cmath>

namespace sim {

// Granary marker positions: a ring of small structures around the
// settlement's cell centre, spaced by the golden angle with a per-cell
// integer phase. Defined ONCE here and mirrored exactly in
// shaders/globe.frag (the granary block in hutsNear) so drawing and the
// tooltip cannot drift.
// Granaries stand among the houses, on the same golden-angle scatter, each
// one a little further out than the last.
constexpr float GRANARY_R0_KM = 0.03f, GRANARY_DR_KM = 0.008f;

// How wide the houses stand: a hut to a household, and enough ground under
// them to walk between. Real distances -- a longhouse is 8 m across and its
// neighbour stands 15 m off, so a village of sixty households is a couple of
// hundred metres end to end, not a kilometre. Mirrored in globe.frag
// villageRadiusKm, and hutCount in the hut count of hutsNear.
inline int hutCount(float P) {
    int n = (int)(P / 12.0f + 0.5f);
    return n < 3 ? 3 : (n > 60 ? 60 : n);
}
inline float villageRadiusKm(float P) { return 0.02f + 0.011f * std::sqrt((float)hutCount(P)); }
// Where the fields begin: outside the houses, with room to walk between.
// Mirrored in globe.frag fieldsNear (the village's plots).
inline float fieldInnerKm(float P) { return villageRadiusKm(P) * 1.2f; }
inline terrain::V3 granaryPos(int cell, int k) {
    terrain::V3 c = cellCentre(cell);
    terrain::V3 east = norm3({-c.y, c.x, 0.0f});
    terrain::V3 north = {c.y * east.z - c.z * east.y, c.z * east.x - c.x * east.z,
                         c.x * east.y - c.y * east.x};
    float a = 2.39996f * k + (float)(cell % 628) * 0.01f;
    float r = (GRANARY_R0_KM + GRANARY_DR_KM * k) / constants::EARTH_RADIUS_KM_F;
    return norm3(c + (east * std::cos(a) + north * std::sin(a)) * r);
}

// Where farmstead slot k stands: the same golden-angle scatter as the
// granaries, at field scale -- kilometres out, each further than the last,
// the outermost just inside where the commute value reaches zero. Defined
// once here and mirrored exactly in shaders/globe.frag, in the farmstead
// blocks of hutsNear and fieldsNear.
inline terrain::V3 farmsteadPos(int cell, int k) {
    terrain::V3 c = cellCentre(cell);
    terrain::V3 east = norm3({-c.y, c.x, 0.0f});
    terrain::V3 north = {c.y * east.z - c.z * east.y, c.z * east.x - c.x * east.z,
                         c.x * east.y - c.y * east.x};
    float a = 2.39996f * k + (float)(cell % 628) * 0.01f + 1.1f; // offset from the granary ring
    float r =
        (population::FSTEAD_R0_KM + population::FSTEAD_DR_KM * k) / constants::EARTH_RADIUS_KM_F;
    return norm3(c + (east * std::cos(a) + north * std::sin(a)) * r);
}

// What the standing fields feed, cached as farmK, and where the next plot
// would go. Each site's tilled plots are priced at ITS OWN cell's
// suitability -- a farmstead on good grass opens good land; one whose slot
// fell on scree or water opens nothing, which is the map talking. Also
// decides the next work order's site (the village's daily-walk disc first,
// then each farmstead's block in the order they stand), how many farmstead
// slots the claim can hold, and whether the next slot is worth building on.
// It reads only the land that stands: a block gone back to the wild
// (population::stepReversion) feeds nothing and is room for the next plot.
// Recomputed on every wake; a few map lookups.
inline void updateFarmland(population::Field& pf, population::Settlement& s) {
    float k = s.sFarm * s.tilled[0];
    int next = (s.sFarm > 0.05f && s.tilled[0] < population::VILLAGE_FIELDS_KM2 - 0.01f) ? 0 : -1;
    int n = std::min((int)(s.farmsteads + 0.5f), population::FSTEAD_MAX);
    for (int i = 0; i < n; i++) {
        float suit = pf.sFarmMap[cellOf(farmsteadPos(s.cell, i))];
        k += suit * s.tilled[i + 1];
        if (next < 0 && suit > 0.05f && s.tilled[i + 1] < population::FSTEAD_KM2 - 0.01f)
            next = i + 1;
    }
    s.farmK = k * population::TILLED_YIELD_PKM2;
    s.tillSiteNext = (int8_t)next;
    // How far out the farmstead spiral stays inside the claim: slots are
    // taken in order, so the first one past the border ends the count.
    terrain::V3 c = cellCentre(s.cell);
    int slots = 0;
    for (int i = 0; i < population::FSTEAD_MAX; i++) {
        terrain::V3 q = farmsteadPos(s.cell, i);
        if (distKm(q, c) > claimReach(s, q)) break;
        slots = i + 1;
    }
    s.fsteadMax = (uint8_t)slots;
    s.fsteadNextOk = n < slots && pf.sFarmMap[cellOf(farmsteadPos(s.cell, n))] > 0.05f;
}

// Fields around a settlement. Stone tools did not stop the first farmers
// clearing woodland -- the axe girdles, the fire does the work, and the ash
// manures the first crop -- so the mark a farming village leaves is a
// clearing, not a patch of open ground it happened to find. Area: roughly
// 0.4 ha under crop feeds a person at Neolithic yields, and with a long
// fallow (a rotation of some twenty years) the land inside the rotation is
// about eight hectares a head (population::FARM_KM2_PER_PERSON). That whole
// mosaic -- crop, stubble, scrub regrowth, the trees not yet taken -- is
// what farming looks like from above, so it is the footprint drawn.

// The plots are drawn individually by the shader (one patch per built
// km2, on a sunflower spiral working outward from the village and from
// each farmstead -- the spiral is inner-out, so clearing visibly accretes).
// This radius is the spiral's reach for the plots that stand: the tooltip
// pick and any coarse test use it as the fields' extent.
inline float farmRadiusKm(const population::Settlement& s, double now) {
    (void)now;
    if (s.tilled[0] <= 0) return 0.0f;
    float inner = fieldInnerKm(s.P);
    // The spiral's packing makes its reach the annulus radius; the margin
    // is a plot's own half-width past its centre.
    return std::sqrt(inner * inner + s.tilled[0] / constants::PI_F) + 0.6f;
}

} // namespace sim
