# Conflict

**Status:** Implemented — see [[Meta/Status Vocabulary]]

Why people fight, and what it costs them. Decided 2026-08-29.

---

## The trigger: nowhere to go

Conflict is not a mood or a trait. It is what happens to a group that **must move or divide and has nowhere to go** — the one branch of [[Design/Migration]]'s decision tree that previously ended in a quiet stall (`noProspect`: we looked, the world is full, we endure).

This is Carneiro's circumscription theory, and the simulation was already computing the variable it turns on. Where land is open, groups fission and walk away; where land is bounded — islands, oasis strings, valleys walled by desert or mountain — the pressure has nowhere to escape to, and it comes out as violence. So the geography of war is emergent: it appears in the places circumscription theory predicts, and it appears **late**, because `noProspect` is rare in an empty world and common in a full one. The Paleolithic stays quiet; the crowded world turns nasty.

One look around is not enough: a raid only follows a *second* consecutive failed survey, so a single disappointing season does not start a war.

## Raids are journeys with a task

A raid is not an event resolved between neighbours in the abstract. It is the second purpose of the project's first agent (the band, from [[Design/Migration]]), and it has to be carried out:

1. **The party leaves.** ~30% of the settlement's people, with their share of its bows and provisions. They are not producing food at home while they are away.
2. **It travels.** Distance is a real cost, so only settlements within contact range (160 km) are worth robbing — you rob the people you know about. A raiding party never rests and never founds a settlement on good ground it passes.
3. **It fights on arrival**, and only on arrival.
4. **It carries the loot home**, which is the part that makes raiding fail interestingly: winning is not the same as profiting.

If the mark has relocated by the time they get there — settlements move constantly — the party turns for home. If *their own* settlement has moved or died while they were away, they join whoever will have them, like any failed migrant band.

## Fighting, and why it is cheap in lives

Strength counts people by who they are (see [[Design/Population]]'s demography), then arms them with bows: men 1.0, women 0.15, children and elderly 0.05, the whole scaled by `0.3 + 0.7 · bow coverage · archery expertise`. Archery is therefore dual-use, and bow-making becomes a genuine choice between hunting better and being harder to rob.

**A raiding party is men** — 55% of them, so the rest stay home; sending 30% of a settlement's *people* would have been every man it had. The old flat 1.3× defender bonus is gone, because the defender's advantage is now explicit: it is the rest of the population, who count for something even though they are not fighting men. What the attacker gets instead is **initiative** (1.5×) — surprise, the choice of the moment, and the freedom to break off. The outcome is **heavily chanced**: odds go as strength^1.5, so twice the strength wins about seven times in ten, not always.

Raiders only set out against someone they can beat and a haul worth the walk, which — with non-combatants properly weighted — rejects most neighbours outright. Tuning note: when the cohort weights first landed, defence fell to ~40% of headcount and the old "attack anyone under twice your strength" guard let settlements rob neighbours 1.4× their size; raids went from 577 per millennium to 182,000. The guard is now simply "we are the stronger".

Casualties fall on the men who are in the fight (a quarter as much on everyone else, on the defending side), so **a settlement that loses its men is crippled for a generation** — and the demographic flows are what let that scar heal slowly rather than instantly.

Casualties are deliberately low (3% of the winner, 8% of the loser) because **people run rather than fight to the end**. The consequence matters more than the realism: repeated raiding *impoverishes* rather than annihilates, and impoverishment is exactly what pushes a group to move. Violence became a redistribution and pressure mechanism instead of an extinction one. (Whether a group stands or runs should eventually depend on what it has to lose; deferred.)

## What can be taken

- **Stores**, up to 60% of what is found, and no more than the party can physically carry (30 days' rations per raider). A granary full of grain cannot be stolen by forty people, only tapped.
- **Livestock**, 35% of the herd, with **no carry limit at all** — cattle walk home on their own legs. This asymmetry is the reason cattle raiding dominates the ethnographic record, and it makes husbandry a liability as well as an asset.

Nothing else changes hands: no land, no captives, no tribute. A raid is theft, not conquest.

## Retaliation comes free

A raided settlement has lost its stores, which is precisely the hunger that [[Design/Migration]]'s trigger already reads. Counter-raids therefore emerge from the existing machinery with no grievance memory, no feud state, and no new code.

## Measured

With demography (2026-08-29): 4,142 raids in a millennium across ~1,240 settlements — about one per settlement every three centuries, episodic rather than habitual — of which 3,049 succeeded and 1,085 were beaten off. Raiders choose fights they can win and still lose one in four. Migration was unaffected (4,321 moves, 963 splits) and the world grew normally (1,239 settlements, 374k people): no death spiral, which is the low-casualty rule doing its job.

## Deferred

- Willingness to stand and fight as a variable, rather than fixed flee rates.
- Defensive works (walls as buildings, on the granary template).
- Intercepting a loaded raiding party on its way home — tempting, but it needs bands to see each other.
- Conquest, tribute, captives, alliances, and any lasting political relationship.
- Raiding bands as targets, and raids on the move against migrating groups.
