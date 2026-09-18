# 11 — Every labour draw comes out of the one budget

**Status:** open (2026-09-18)

## Problem

[[Design/Resources]] says the labour ledger is one capped budget of man-days and that
"everything a settlement does — procuring food, gathering fuel, building, crafting — is a
fractional allocation of that budget", spent in a priority stack: food to need, then heat to
need, then construction and crafts from what is left.

The code implements that for food and heat only. Granary building, farmstead raising, plot
clearing and bow carving each take an independent share of the headcount, and none of them
consults the budget or what the earlier claims already spent. A settlement clearing a plot
while raising a farmstead, building a granary and carving bows spends 2 + 2 + 2 + 5 = 11% of
its people on crafts on top of a food day that may already have used the whole daylight
budget — labour the day does not contain. The shares are small enough that the world totals
look sane, which is why this has not shown up as a number; it is wrong in principle and it
gets worse with every future thing that is built.

Violates `standards/general.md` §Documentation (the note and the code disagree) and the
ledger's own stated rule.

## Evidence

- `src/population.h:498-525` `stepHeat` is the only function that computes the day's budget
  (`budget = st.P * st.wh / 12.0f`, line 501) and what the food work leaves free
  (`freeMD`, line 507). Both are local; neither is stored on `Step` for a later stage.
- `src/population.h:573-594` `stepBuilding` takes three draws — `P * GRANARY_LABOUR_SHARE`
  (576), `P * FSTEAD_LABOUR_SHARE` (581), `P * TILL_LABOUR_SHARE` (586) — each a share of
  the headcount, none of the budget. Its only gate is `fill > HOARD_FILL` (574), which is
  the famine pre-emption, not an allocation.
- `src/population.h:599-606` `stepBows` takes `P * BOW_LABOUR_SHARE` (604) with no gate at
  all: bows are carved in famine, unlike every other build.
- `src/settlement.h:203, 297, 329, 363` the four shares, each documented as "share of
  people", none as a share of the budget.
- `Settlement::labFuel` (`src/settlement.h:654`) is the one allocation readout that exists;
  there is no equivalent for the crafts, so the panel cannot show the split either.

Line references checked against commit `8d974f3` on 2026-09-18.

## Design

[[Design/Resources]] §The labour ledger, at status Implemented — specifically the
"Priority stack" paragraph and "Work is capped and allocated, never conjured". No design
change: this order makes the code state what the note already decided. The note's own
sentence "Occupational structure is the readout of the allocation, not a mechanism" is what
step 4 below makes true for crafts as well as fuel.

## Recommended change

The seam is `Step`: it already carries the sub-step's derived state between stage functions.

1. `stepHeat` stores the day's budget and what food left free on `Step` (`labBudget`,
  `labFree`) instead of keeping them local, and spends its own `cutMD` out of `labFree`.
  Keep the heat arithmetic identical so the probe does not move. One commit, no behaviour
  change.
2. `stepBuilding` and `stepBows` draw from `st.labFree` and decrement it in the stack's
  order (granary, farmstead, clearing, bows — the order they stand in now). A draw takes
  the lesser of its share and what is left, so the existing shares become ceilings rather
  than independent claims.
3. `stepBows` gets the same `fill > HOARD_FILL` gate as the builds, or the deviation is
  written down in the code where it stands. Hunger stopping bow-carving is a behaviour
  change and the probe will move; that is the one number this order is allowed to move.
4. Record the crafts' share on `Settlement` next to `labFuel` (one float, the summed craft
  allocation) so the panel can show it and the probe can assert on it.

## Files

- `src/population.h` (`Step`, `stepHeat`, `stepBuilding`, `stepBows`)
- `src/settlement.h` (the four constants' comments; one new readout field)
- `src/inspect.h` (the panel line beside the woodcutter share)
- `src/test_resources.cpp` (the assertion in Done when)

Overlaps order 04's files if that is still open; queue after it.

## Done when

- `build_testresources.bat` builds and `build\test_resources.exe 7 700` runs.
- The probe prints a new line: the maximum over all settlements of (summed allocations /
  day's budget), and the count of settlements over 1.0. **The count is zero.**
- Settlements, people and tilled km2 at year 700 are within 2% of the pre-change run for
  the same seed, with the bow gate the only intended difference; the run quotes both.

## Depends on

Order 04 (split population and sim), if its `population.h` changes have not merged.
