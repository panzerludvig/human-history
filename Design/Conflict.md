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

Strength is people armed by their bows: `P · (0.3 + 0.7 · bow coverage · archery expertise)`. Archery is therefore dual-use, and bow-making becomes a genuine choice between hunting better and being harder to rob.

The defender gets a 1.3× edge — home ground, familiar terrain, and non-combatants who can be hidden rather than defended — and the outcome is **heavily chanced**: odds go as strength^1.5, so twice the strength wins about seven times in ten, not always. Raiders pick targets they think they can beat, and still lose more often than they win.

Casualties are deliberately low (3% of the winner, 8% of the loser) because **people run rather than fight to the end**. The consequence matters more than the realism: repeated raiding *impoverishes* rather than annihilates, and impoverishment is exactly what pushes a group to move. Violence became a redistribution and pressure mechanism instead of an extinction one. (Whether a group stands or runs should eventually depend on what it has to lose; deferred.)

## What can be taken

- **Stores**, up to 60% of what is found, and no more than the party can physically carry (30 days' rations per raider). A granary full of grain cannot be stolen by forty people, only tapped.
- **Livestock**, 35% of the herd, with **no carry limit at all** — cattle walk home on their own legs. This asymmetry is the reason cattle raiding dominates the ethnographic record, and it makes husbandry a liability as well as an asset.

Nothing else changes hands: no land, no captives, no tribute. A raid is theft, not conquest.

## Retaliation comes free

A raided settlement has lost its stores, which is precisely the hunger that [[Design/Migration]]'s trigger already reads. Counter-raids therefore emerge from the existing machinery with no grievance memory, no feud state, and no new code.

## Measured

First millennium-scale run: 577 raids launched, 235 successful and 341 beaten off, 576 parties home. Migration was unaffected (4,210 moves, 1,005 splits) and the world grew normally (1,287 settlements, 369k people) — no death spiral, which is the low-casualty rule doing its job.

## Deferred

- Willingness to stand and fight as a variable, rather than fixed flee rates.
- Defensive works (walls as buildings, on the granary template).
- Intercepting a loaded raiding party on its way home — tempting, but it needs bands to see each other.
- Conquest, tribute, captives, alliances, and any lasting political relationship.
- Raiding bands as targets, and raids on the move against migrating groups.
