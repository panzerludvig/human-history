# 11 — Every labour draw comes out of the one budget

**Status:** planned (2026-09-28)

Line references checked against 5087977 on 2026-09-28.

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

Line references checked against commit `8d974f3` on 2026-09-18; re-checked against
`f9c731f` on 2026-09-26 and `5087977` on 2026-09-28, all unchanged.

## Design

[[Design/Resources]] §The labour ledger — the "Priority stack" paragraph, "Work is capped
and allocated, never conjured", and "Sharing the surplus among projects" (designed
2026-09-28: when the surplus falls short, every project is scaled by the same fraction).
The note's sentence "Occupational structure is the readout of the allocation, not a
mechanism" is why the crafts get a readout as fuel has.

## Outcome

- **One budget.** Food, then heat, then the four projects (granary building, farmstead
  raising, plot clearing, bow carving) draw on the same day's man-days, in that order. The
  projects share what food and heat leave, and never more.
- **The shares become ceilings.** Each project's existing share of the people is the most
  it takes. When what is left covers every ceiling, nothing changes from today. When it
  does not, every project is scaled by the same fraction.
- **Famine stops all four.** Bows stop in famine like the builds; no project draws labour
  when stores are at the hoarding threshold.
- **The heat arithmetic is unchanged**, so a settlement's fuel and its cold deaths come out
  as before.
- **A readout.** Each settlement records the share of the day's labour that went to the
  projects, beside `labFuel`, and the settlement panel shows it next to the woodcutters'
  line.
- The comments on the four share constants say they are ceilings on the surplus, not
  shares of the people.

## Files

A guide, not a limit:

- `src/population.h` (`Step`, `stepHeat`, `stepBuilding`, `stepBows`)
- `src/settlement.h` (the four constants' comments; the readout field)
- `src/inspect.h` (the panel line beside the woodcutters, `inspect.h:393-395`)
- `src/test_resources.cpp` (the check in Done when)
- `Technical/Globe Viewer.md` (the granary paragraph, which describes build pace)

## Done when

1. `build.bat` and `build_testresources.bat` exit 0 with no warning beyond the existing
   C4996 set.
2. `build\test_resources.exe 7 700` and `3 700` print a new line: the maximum over all
   settlements and sub-steps of (labour allocated to food, heat and the projects) / (the
   day's budget), and the count of settlement-days over 1.0 and of project labour drawn
   while stores were at or below the hoarding threshold. **Both counts are zero.**
3. A sanity guard, not a target: settlements and people at year 700 are within 10% of the
   pre-order run for the same seed. The run quotes both runs' totals (settlements, people,
   tilled km2, granaries, farmsteads) for the morning.

## Depends on

Nothing.
