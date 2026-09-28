# 13 — Farmsteads: placed by the ground, removed one by one, drawn wherever they stand

**Status:** planned (2026-09-28)

Line references checked against 5087977 on 2026-09-28.

## Problem

Every settlement's farmsteads sit on the same golden-angle spiral, so every farming village
looks alike from the air, and the spiral is computed in three places (the simulation and
twice in the shader) that do not agree about what a slot is:

1. **A bad slot stops expansion for good.** The simulation tests only the next slot; if it
   falls on water or scree, the settlement never builds another farmstead, even with prime
   grass in every slot after it.
2. **Houses can be drawn on water.** The simulation prices a slot at its 20 km cell; the
   shader draws a house for every slot up to the count with no land test. (Rarer than first
   stated: fields are never drawn on water, and a house needs a lake or coast point in a
   cell priced as farmable. Unconfirmed in practice.)
3. **Farmsteads vanish from the map.** The shader finds farmsteads through one village per
   20 km cell; where two villages' farmsteads share a cell, the last written wins and the
   other's houses and far fields are not drawn.
4. **Only the newest farmstead can go.** Farmsteads are a count, so removing any but the
   last would move every farmstead after it. Order 12 needs the farthest one to go first.

Violates `standards/general.md` §Mirrored code (three copies of the placement, and the
simulation's land test has none) and the design note's own sentence that "a slot that falls
on scree or water opens nothing".

## Evidence

- `src/farmland.h:46` `farmsteadPos(cell, k)` — the spiral; mirrored in `shaders/globe.frag`
  in `hutsNear` (`globe.frag:525-564`) and `fieldsNear` (`globe.frag:700-737`).
- `src/farmland.h:86` — `fsteadNextOk` tests only slot `n` (defect 1); `farmland.h:69` prices
  a slot at its cell's `sFarmMap`.
- `src/textures.h:73-75` — one village cell per 20 km cell in the pop texture's alpha, last
  writer wins (defect 3).
- `src/settlement.h:656` — `float farmsteads`, a count in slot order; `settlement.h:660`
  `tilled[1 + FSTEAD_MAX]` per slot; `FSTEAD_MAX = 20` (`settlement.h:330`).
- `src/inspect.h:166` — the tooltip pick walks the same spiral.
- `src/bands.h:683-688` — a settlement that leaves as a whole leaves one ruin at its own
  cell; its farmsteads leave none.
- `src/population.h:550-551` (`stepFillCycle`) — a farmstead is ordered when `fsteadNextOk`
  holds.

## Design

[[Design/Technology]], "Where farmsteads stand, and how they end", designed 2026-09-28, at
the end of §Farming's reach, tilled plots, and farmsteads; and "Fields go back to the wild"
(order 12) for when fields revert.

## Outcome

- **A chain, not a count.** Each settlement keeps its farmsteads as a chain of places in
  founding order, each a farm or a ruin with the date it became one. The chain is saved;
  positions are not.
- **Placement by the ground.** The next place is the best of a handful of candidates drawn
  from the settlement's seed, scored by nearness to the core (beyond the village fields,
  within the day's reach), distance from the settlement's own earlier places, and the
  terrain at the exact point: never water, steep ground avoided, flat land preferred. How
  many candidates and how the scores weigh is the implementer's tuning, reported in the
  Run section.
- **Positions never move in play.** A place is computed from the settlement's seed, the
  village centre, the terrain and the places before it, nothing else. Other settlements
  and the claim border do not enter it.
- **One placement, on the CPU.** The shader and the tooltip use the positions the CPU
  computes; the spiral and its two shader copies are gone.
- **Farms end as ruins, individually.** A farm whose fields are gone becomes a ruin, except
  a farm that has not yet had its first field; one that has had none within 10 years of
  its founding becomes a ruin all the same. When a settlement cannot tend all its fields,
  the farm farthest from the village loses its fields first (order 12's rule, made exact
  by real positions).
- **Ruins are reoccupied first.** A new farmstead rebuilds the ruin nearest the village, at
  the full cost, before a new place is added. Ruins disappear from the map
  `RUIN_LIFE_DAYS` after abandonment; the place stays in the chain, empty, and can be built
  on again.
- **A settlement that moves leaves its farms as ruins** where they stood, recorded with
  their positions like a settlement's ruin, and starts a new chain.
- **Every standing farmstead and its fields are drawn**, however many settlements'
  farmsteads share a stretch of ground; farmstead ruins are drawn as ruins.
- **Saves keep working.** A save from before this order loads; its farmsteads become a chain
  of farms whose positions are computed by the new placement, so they appear in new
  places once, on that load.
- `Technical/Globe Viewer.md` describes the placement and the drawing.

## Files

A guide, not a limit: `src/farmland.h`, `src/settlement.h`, `src/population.h`,
`src/bands.h` (relocation), `src/textures.h`, `shaders/globe.frag` (`hutsNear`,
`fieldsNear`, ruins), `src/inspect.h`, `src/overlay.h` if it marks farmsteads,
`src/savefile.h`, `src/test_resources.cpp`, `src/test_savefile.cpp`,
`Technical/Globe Viewer.md`.

## Done when

1. `build.bat`, `build_testresources.bat` and `build_testsavefile.bat` exit 0 with no
   warning beyond the existing C4996 set.
2. `build\test_resources.exe 7 700` and `3 700` print, and each is **zero**:
   - standing farmsteads whose point is sea or lake;
   - farmstead positions that changed between one year and the next without their
     settlement moving (the probe recomputes every position yearly and compares);
   - farms abandoned before their first field and within 10 years of founding;
   - farmstead ruins still present more than `RUIN_LIFE_DAYS` after abandonment;
   - standing farmsteads that the drawing lookup does not find from their own position
     (checked on the CPU against the data the shader is given).
3. The same runs print how many farms became ruins, how many ruins were reoccupied, and the
   largest number of settlements whose farmsteads share one 20 km cell; quoted.
4. `build\test_savefile.exe 7 3` passes with the chain round-tripping, and a save written
   before this order loads.
5. A sanity guard, not a target: standing farmsteads at year 700 are at least 80% of the
   pre-order run for the same seed. The Run quotes both.
6. Screenshots of a farming village at close zoom (`HH_BENCH`, the village views of order
   06's Done when) before and after, for the morning: the check that villages no longer
   look alike is made in the game.

## Depends on

12: farms become ruins when their fields are gone, and fields go only by order 12's rule.
