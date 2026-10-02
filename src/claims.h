// The claim: the ground a settlement works, kept from everyone else --
// sixteen sectors of reach that grow with need until they meet a neighbour's
// (settlement.h, "territory"). Who holds a point, how much room a newcomer
// would have, what a claim of a given reach is worth, how far a frontier
// creeps in a year. Design/Migration.md.
#pragma once
#include "constants.h"
#include "technology.h"
#include "sphere.h"
#include <cmath>

namespace sim {

// The claim, sector by sector. Sector 0 faces east and they turn north.
inline terrain::V3 sectorDir(const terrain::V3& c, int k) {
    terrain::V3 east = norm3({-c.y, c.x, 0.0f});
    terrain::V3 north = {c.y * east.z - c.z * east.y, c.z * east.x - c.x * east.z,
                         c.x * east.y - c.y * east.x};
    float a = (2 * constants::PI_F) * (k + 0.5f) / population::CLAIM_SECTORS;
    return norm3(east * std::cos(a) + north * std::sin(a));
}

// How far settlement `s` reaches towards the point `q`. Mirrored in
// globe.frag claimReach.
inline float claimReach(const population::Settlement& s, const terrain::V3& q) {
    terrain::V3 c = cellCentre(s.cell);
    terrain::V3 east = norm3({-c.y, c.x, 0.0f});
    terrain::V3 north = {c.y * east.z - c.z * east.y, c.z * east.x - c.x * east.z,
                         c.x * east.y - c.y * east.x};
    terrain::V3 d = {q.x - c.x, q.y - c.y, q.z - c.z};
    // Blended between the two nearest sectors, so a claim is a closed curve
    // rather than sixteen arcs with steps between them -- and so what the
    // map draws is exactly what the simulation enforces.
    float a = std::atan2(d.x * north.x + d.y * north.y + d.z * north.z,
                         d.x * east.x + d.y * east.y + d.z * east.z) /
                  (2 * constants::PI_F) * population::CLAIM_SECTORS -
              0.5f;
    float fl = std::floor(a), f = a - fl;
    int k0 = ((int)fl % population::CLAIM_SECTORS + population::CLAIM_SECTORS) %
             population::CLAIM_SECTORS;
    int k1 = (k0 + 1) % population::CLAIM_SECTORS;
    return s.claim[k0] * (1.0f - f) + s.claim[k1] * f;
}

// Who holds this ground, if anyone. `skip` is the settlement doing the asking.
inline int claimant(const population::Field& pf, const terrain::V3& q, int skip) {
    int cell = cellOf(q), cx = cell % population::W, cy = cell / population::W;
    float lat = std::asin(std::clamp(q.z, -1.0f, 1.0f));
    int rx = std::min((int)std::ceil(2.5f / std::max(std::cos(lat), 0.05f)) + 1, population::W / 2);
    for (int dy = -3; dy <= 3; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= population::H) continue;
        for (int dx = -rx; dx <= rx; dx++) {
            int si = pf.settlementAt[y * population::W + hydrology::wrapX(cx + dx)];
            if (si < 0 || si == skip || pf.settlements[si].leaving) continue;
            const population::Settlement& o = pf.settlements[si];
            if (distKm(q, cellCentre(o.cell)) < claimReach(o, q)) return si;
        }
    }
    return -1;
}

// How far a newcomer here could claim before meeting somebody: the room
// left between this ground and the nearest claim, capped at what one place
// can ever hold. Room is the whole reason one site is worth more than
// another with the same soil -- land you cannot claim feeds nobody.
inline float roomKm(const population::Field& pf, terrain::V3 n) {
    float room = population::CLAIM_CAP_KM;
    int cell = cellOf(n), cx = cell % population::W, cy = cell / population::W;
    float lat = std::asin(std::clamp(n.z, -1.0f, 1.0f));
    int rx = std::min((int)std::ceil(5.5f / std::max(std::cos(lat), 0.05f)) + 1, population::W / 2);
    for (int dy = -6; dy <= 6; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= population::H) continue;
        for (int dx = -rx; dx <= rx; dx++) {
            int si = pf.settlementAt[y * population::W + hydrology::wrapX(cx + dx)];
            if (si < 0 || pf.settlements[si].leaving) continue;
            const population::Settlement& o = pf.settlements[si];
            room = std::min(room, distKm(n, cellCentre(o.cell)) - claimReach(o, n));
            if (room <= 0) return 0.0f;
        }
    }
    return room;
}

// Enough unclaimed room to hold a new settlement's floor claim.
inline bool claimFits(const population::Field& pf, terrain::V3 n) {
    return roomKm(pf, n) >= population::CLAIM_FLOOR_KM;
}

// What a claim of this reach is worth against the fixed catchment every
// settlement used to be handed: 1 at the floor, about 3.3 at the cap.
inline float claimFactor(float reachKm) {
    return population::claimYieldKm2(
               std::clamp(reachKm, population::CLAIM_FLOOR_KM, population::CLAIM_CAP_KM)) /
           population::FORAGE_KM2;
}

// What the claim is worth, and the yields that follow from it. A settlement
// eats what its ground produces, so its cached capacities are the per-cell
// figures scaled by the claim -- at the floor that is exactly the fixed
// catchment every settlement used to be handed.
inline void applyClaim(population::Field& pf, population::Settlement& s) {
    float v = 0;
    for (int k = 0; k < population::CLAIM_SECTORS; k++) v += population::claimYieldKm2(s.claim[k]);
    s.claimKm2 = v / population::CLAIM_SECTORS;
    float f = s.claimKm2 / population::FORAGE_KM2;
    s.kFoodP = pf.kFoodPMap[s.cell] * f;
    s.kGame = pf.kGameMap[s.cell] * f;
    s.kSmall = pf.kSmallMap[s.cell] * f;
    s.kFish = pf.kFishMap[s.cell] * f;
    s.kWater = pf.kWaterMap[s.cell] * f;
}

inline void claimFloor(population::Field& pf, population::Settlement& s) {
    for (int k = 0; k < population::CLAIM_SECTORS; k++) s.claim[k] = population::CLAIM_FLOOR_KM;
    applyClaim(pf, s);
}

// How far a group of this size would have to reach on this ground to feed
// itself, with something in hand for the people coming. kFoodP already
// carries the sustain ratio, so the per-km2 figure it gives is people
// supported -- counting the ratio again here asked for half the land that
// was needed, and no claim ever wanted to grow.
inline float wantedReachKm(const population::Field& pf, int cell, float P, float R) {
    // What a km2 of this ground actually feeds: its yield at the condition
    // the land is in. Lived-on land settles at about SUSTAIN_R, so a
    // settlement at equilibrium holds only half the people its pristine
    // yield suggests -- price the need at the pristine figure and every
    // claim in the world decides it already has land to spare.
    float perKm2 = pf.kFoodPMap[cell] / population::FORAGE_KM2 * std::max(R, 0.2f);
    if (perKm2 <= 0 || P <= 0) return population::CLAIM_FLOOR_KM;
    float want = population::claimRadiusFor(P / perKm2 * population::CLAIM_MARGIN);
    return std::clamp(want, population::CLAIM_FLOOR_KM, population::CLAIM_CAP_KM);
}

// A frontier creeps outward: people work further out each year than they
// did, faster when they are hungry, and stop where somebody else was first.
// Borders do not move once they meet. Returns true if anything is still
// free to grow -- a settlement that has nowhere left to widen is hemmed in,
// which is what turns pressure into emigration and, later, into a fight.
inline bool growClaim(population::Field& pf, int si, double now) {
    population::Settlement& s = pf.settlements[si];
    if (s.leaving || s.P <= 0) return false;
    double span = std::max(now - s.claimT, 0.0);
    s.claimT = now;
    float want = wantedReachKm(pf, s.cell, s.P, s.R);
    // Most settlements, most of the time, already reach as far as they want
    // to: check that before walking the frontier, which is the expensive part.
    bool wants = false;
    for (int k = 0; k < population::CLAIM_SECTORS && !wants; k++) wants = s.claim[k] < want - 0.01f;
    // Not wanting more land is not the same as having room for more: a
    // settlement that already reaches as far as it needs, or as far as one
    // place can, is done expanding, and pressure has to find another outlet.
    if (!wants) return false;
    // Hunger pushes the border: people range further before they leave.
    float phi = s.P > 1 ? technology::effectiveK(s, now) * s.R / s.P : 2.0f;
    float step = (float)(population::CLAIM_GROW_KM_YR * span / constants::DAYS_PER_YEAR) *
                 (1.0f + 6.0f * population::needRamp(phi));
    terrain::V3 c = cellCentre(s.cell);
    bool moved = false, free = false;
    for (int k = 0; k < population::CLAIM_SECTORS; k++) {
        if (s.claim[k] >= want - 0.01f) continue;
        float r = std::min(s.claim[k] + std::max(step, 0.01f), want);
        terrain::V3 u = sectorDir(c, k);
        terrain::V3 q = norm3(c + u * (r / constants::EARTH_RADIUS_KM_F));
        if (claimant(pf, q, si) >= 0) continue; // somebody was here first
        free = true;
        if (step <= 0) continue;
        s.claim[k] = r;
        moved = true;
    }
    if (moved) applyClaim(pf, s);
    return free;
}

} // namespace sim
