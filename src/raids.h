// Raiding (Design/Conflict.md): the answer for a people that must move or
// divide with nowhere to go. Who is worth robbing, whether a party sets
// out, what a fight costs when it reaches them. The party itself is a Band
// with a purpose; its march is bands.h.
#pragma once
#include "technology.h"
#include "events.h"
#include "sphere.h"
#include <cmath>
#include <cstdio>

namespace sim {

// What a group can bring to a fight: people, armed by their bows. Archery
// is dual-use, so the bow-making labour is a real choice between hunting
// better and being harder to rob.
// People weighted by who they are, then armed by their bows. The rest of
// a settlement's people are why it is harder to rob than a raiding party
// of the same headcount is to beat -- so there is no separate defender
// bonus any more; the advantage is the population itself.
inline float fightStrength(const population::Cohorts& c, float bows, float archExp,
                           float fightAff = 0) {
    float able = population::cohortStrength(c);
    float cover = population::bowCoverage(bows, std::max(c.M, 1.0f)); // bows are carried by the men
    return able *
           (population::FIGHT_UNARMED + (1.0f - population::FIGHT_UNARMED) * cover * archExp) *
           population::affinityBonus(fightAff);
}

// The raid: reached them, now settle it. Heavily chanced -- a party twice
// the strength wins about seven times in ten, not always -- and cheap in
// lives, because people run rather than fight to the end. What is taken is
// limited by what can be carried, except livestock, which walks itself.
inline void resolveRaid(population::Field& pf, technology::WorldState& ws, population::Band& b,
                        int ti, double now) {
    population::Settlement& t = pf.settlements[ti];
    float aExp = technology::expertise(b.tech[population::TECH_ARCHERY], now);
    float dExp = technology::expertise(t.tech[population::TECH_ARCHERY], now);
    float A = fightStrength(b.pop, b.bows, aExp, b.aff.fight) * population::RAID_INITIATIVE;
    float D = fightStrength(t.pop, t.bows, dExp, t.aff.fight);
    double pa = std::pow(std::max(A, 1e-3f), population::RAID_ODDS_POWER);
    double pd = std::pow(std::max(D, 1e-3f), population::RAID_ODDS_POWER);
    bool won = technology::urand(ws.rng) < pa / (pa + pd);
    float lossA = won ? population::RAID_LOSS_WINNER : population::RAID_LOSS_LOSER;
    float lossD = won ? population::RAID_LOSS_LOSER : population::RAID_LOSS_WINNER;
    // The men are the ones in the fight; a few of the rest are caught up
    // in it. A settlement that loses its men is crippled for a generation,
    // and the flows above are what let that scar heal slowly.
    float beforeA = b.P, beforeD = t.P;
    b.pop.M *= 1.0f - lossA;
    b.P = b.pop.total();
    t.pop.M *= 1.0f - lossD;
    t.pop.C *= 1.0f - lossD * 0.25f;
    t.pop.W *= 1.0f - lossD * 0.25f;
    t.pop.E *= 1.0f - lossD * 0.25f;
    t.P = t.pop.total();
    b.bows *= 1.0f - lossA;
    t.bows *= 1.0f - lossD;
    int deadA = (int)std::lround(std::max(beforeA - b.P, 0.0f));
    int deadD = (int)std::lround(std::max(beforeD - t.P, 0.0f));
    if (won) {
        float carry = b.P * population::LOOT_CARRY_DAYS;
        b.loot = std::min(t.S * population::LOOT_STORE_SHARE, carry);
        b.lootHerd = t.herd * population::LOOT_HERD_SHARE;
        t.S = std::max(t.S - b.loot, 0.0f);
        t.herd = std::max(t.herd - b.lootHerd, 0.0f);
    }
    // Both sides learn the trade, whichever way it went. Being raided
    // makes a people dangerous, not merely poorer.
    b.aff.fight += (1.0f - b.aff.fight) * population::FIGHT_LEARN;
    t.aff.fight += (1.0f - t.aff.fight) * population::FIGHT_LEARN;
    {
        char txt[96];
        if (won)
            snprintf(txt, sizeof txt, "%s raided %s: %d rations, %d livestock taken", b.name,
                     t.name, (int)b.loot, (int)b.lootHerd);
        else
            snprintf(txt, sizeof txt, "%s beat off a raid by %s", t.name, b.name);
        // Both sides count their dead. The numbers travel with the event
        // rather than inside its sentence: a line long enough to hold them
        // both runs off the end of the panel that shows it.
        note(pf, won ? population::EV_RAID_HIT : population::EV_RAID_HELD, now, t.id, b.homeId,
             b.id, won ? b.loot : 0.0f, txt, (float)deadD, (float)deadA);
    }
    b.returning = true;
    fprintf(stderr, "raid: %s settlement %u, %d rations %d livestock, %d + %d dead, day %.0f\n",
            won ? "sacked" : "beaten off by", t.id, (int)b.loot, (int)b.lootHerd, deadA, deadD,
            now);
}

// Who is worth robbing: the richest neighbour we could plausibly beat.
// Only settlements within contact range are candidates -- you rob the
// people you know about -- and livestock counts double, being wealth that
// carries itself home.
inline int raidTarget(const population::Field& pf, int si, double now, float strength) {
    const population::Settlement& s = pf.settlements[si];
    int best = -1;
    float bestScore = 0;
    for (int j : pf.neighbours[si]) {
        const population::Settlement& t = pf.settlements[j];
        if (t.leaving || t.P < population::BAND_MIN_P) continue;
        float prize = t.S * population::LOOT_STORE_SHARE +
                      t.herd * population::LOOT_HERD_SHARE * population::LOOT_CARRY_DAYS;
        if (prize < population::RAID_WORTH_IT) continue; // not worth the walk
        float dExp = technology::expertise(t.tech[population::TECH_ARCHERY], now);
        float def = fightStrength(t.pop, t.bows, dExp, t.aff.fight);
        float score = prize / std::max(def, 1.0f);
        if (score > bestScore) {
            bestScore = score;
            best = j;
        }
    }
    // You rob people you can beat. With non-combatants counted at their
    // real weight, a settlement defends far better than its headcount
    // suggests, so this rejects most neighbours outright.
    if (best >= 0) {
        const population::Settlement& t = pf.settlements[best];
        float dExp = technology::expertise(t.tech[population::TECH_ARCHERY], now);
        if (fightStrength(t.pop, t.bows, dExp, t.aff.fight) > strength) return -1;
    }
    return best;
}

// Nowhere to go, and hungry: the third answer. Sends a party to rob the
// best neighbour within reach. Returns true if one set out.
inline bool maybeRaid(population::Field& pf, technology::WorldState& ws, int si, double now) {
    population::Settlement& s = pf.settlements[si];
    if (s.pop.M * population::RAID_MEN_SHARE < population::RAID_MIN_P) return false;
    float archExp = technology::expertise(s.tech[population::TECH_ARCHERY], now);
    population::Cohorts party{};
    party.M = s.pop.M * population::RAID_MEN_SHARE; // the men go; the rest stay
    float strength =
        fightStrength(party, s.bows * population::RAID_MEN_SHARE, archExp, s.aff.fight) *
        population::RAID_INITIATIVE;
    int ti = raidTarget(pf, si, now, strength);
    if (ti < 0) return false;
    terrain::V3 home = cellCentre(s.cell);
    population::Band b{};
    b.id = pf.nextBandId++;
    b.purpose = population::BAND_RAID;
    b.homeId = s.id;
    b.targetId = pf.settlements[ti].id;
    b.px = home.x;
    b.py = home.y;
    b.pz = home.z;
    b.culture = s.culture;
    b.aff = s.aff;
    for (int i = 0; i < 16; i++) b.name[i] = s.name[i];
    b.pop = party;
    b.P = party.total();
    b.bows = s.bows * population::RAID_MEN_SHARE;
    b.S = std::min(s.S * population::RAID_MEN_SHARE, population::CAP_DAYS_BAND * b.P);
    b.water = population::CAP_WATER_DAYS * b.P; // nobody sets out with empty skins
    b.targetCell = pf.settlements[ti].cell;
    b.t = now;
    b.nextUpdate = now + population::BAND_STEP_DAYS;
    for (int t = 0; t < population::NTECH; t++) b.tech[t] = s.tech[t];
    s.pop.M -= party.M;
    s.P = s.pop.total();
    s.S -= b.S;
    s.bows -= b.bows;
    pf.bands.push_back(b);
    {
        char txt[96];
        snprintf(txt, sizeof txt, "%s sent %d warriors against %s", s.name, (int)b.P,
                 pf.settlements[ti].name);
        note(pf, population::EV_RAID_LAUNCH, now, s.id, pf.settlements[ti].id, b.id, b.P, txt);
    }
    logAt("raiding party leaves settlement", si, home, b.P, now);
    return true;
}

} // namespace sim
