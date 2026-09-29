# 11 — Every labour draw comes out of the one budget

**Status:** taken by the night of 2026-09-28; passed, merged into `nightly/2026-09-28` as `44c98f6`

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

## Run

**Outcome:** passed. Branch `wo/11-one-labour-budget-for-every-draw`, one commit
`4314733`. Merged into `nightly` as `44c98f6`, after 06 and 07, cleanly.

What it did: each sub-step keeps one ledger. `stepFoodLabour` opens it with the day's
budget and food's claim (stepHeat's expressions, moved unchanged); `stepHeat` books the
woodcutters from it; `allocateProjects`, after the annual judgement, scales every active
project's ceiling (`*_LABOUR_SHARE` x people) by one fraction when the surplus falls
short, and by zero at or below `HOARD_FILL`, so bows stop in famine too. `closeLedger`
sets `Settlement::labProj` (a readout, not saved) and aborts if food + heat + projects
exceed the budget. The panel shows "Builders and crafts: N% of the day's labour".

Done when:

1. `build.bat` and `build_testresources.bat` exit 0, C4996 only.
2. Seeds 7 and 3, both passes, on the branch and again on `nightly`:
   `ledger: max labour/budget 1.0000; over budget 0 settlement-days (0 sub-steps);
   projects in famine 0 settlement-days (0 sub-steps)`.
3. Year 700, settlements / people, pre-order -> order. On the branch, against `main`:
   seed 7 cold 8123/1985115 -> 8368/1925248, heat 8207/1999743 -> 8699/2069321; seed 3
   cold 8913/2071535 -> 9164/2032831, heat 9178/2169828 -> 9355/2220924 (at most 6%).
   On `nightly`, against `nightly` before the merge: seed 7 cold 8831/2199489 ->
   8477/1914923 (people -12.9%), heat 8370/2092920 -> 8578/2033436; seed 3 cold
   9091/2136860 -> 9114/2000124, heat 9191/2133838 -> 9393/2151738. The seed 7 cold
   figure misses the 10% band. Kept on the developer's decision that the guard measures
   noise: order 06, which changes only ice, moved the same figure +10.8%. The checklist
   no longer gates on world totals (`work_orders/README.md`, 2026-09-28).

Unsure of: farming 2-5x larger in all four branch runs; a diagnostic with bows carving in
famine as before brought the first invention back to the baseline's, so part is the famine
rule and the rest likely divergence. The overdraw abort is live in the game.
`stepBuilding` keeps its own famine check, now redundant. For the game: the panel line.

Review note for the night: [[Dev Log/Nightly/2026-09-28]].
