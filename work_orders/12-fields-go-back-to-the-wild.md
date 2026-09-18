# 12 — Fields go back to the wild when nobody works them

**Status:** open (2026-09-18) — blocked on a design line; see Design below

## Problem

Tilled land became a built stock on 2026-09-14, but nothing ever takes it away. `tilled[]`
only grows. A settlement whose farming practice lapses keeps every square kilometre it ever
cleared, and gets the full yield back on the day practice resumes; a settlement that halves
in population keeps fields no remaining hand tends. The map draws healthy fields around a
village that has not farmed in three generations.

This is the one place where the "means" pattern was not followed. Husbandry zeroes the herd
when practice lapses (`technology.h:434`); farming does nothing, because when that line was
written farming had no means to lose. It has one now.

It also leaves `meansPresent(TECH_FARMING)` testing the wrong thing: it asks whether the
ground could be farmed (`sFarm >= 0.05f`), not whether fields stand — where granaries
already ask whether a granary stands or is going up. So the skill-decay machinery cannot
see a farming people who have lost their fields.

## Evidence

- `src/technology.h:431-434` — practice lapses; `if (tech == TECH_HUSBANDRY) s.herd = 0;`
  is the only means cleared. `tilled[]` is untouched here and nowhere else.
- `src/technology.h:172-175` — `meansPresent` for `TECH_FARMING` returns `s.sFarm >= 0.05f`
  (the ground); compare `TECH_GRANARY` on the next lines, which returns
  `s.granaries > 0 || s.buildWork > 0` (the built thing).
- `src/farmland.h:64-75` — `updateFarmland` computes `farmK` from `tilled[]` with no
  reference to practice or to hands, so the fields feed people whether or not anyone farms;
  `technology.h:149-151` then multiplies it by expertise, which is zero for a lapsed people
  — so the yield is correctly zero while lapsed, and returns in full on re-adoption. The
  stock, not the flow, is what is wrong.
- `src/settlement.h:660` — `float tilled[1 + FSTEAD_MAX]`, written only by
  `population.h:588-591` (the clearing work order) and the save loader.
- Measured: `build\test_resources.exe 7 700` reports 10,209 km2 tilled with 480 settlements
  farming; the 2026-08-31 decay measurement had 522 settlements lapse from farming somewhere
  along the way, every one of which still holds its fields.

Line references checked against commit `8d974f3` on 2026-09-18.

## Design

**This order is not queueable until a design line exists.** Nothing in `Design/` says what
happens to cleared land when it stops being worked, so per `Codex.md` a run cannot decide it.

What must be decided, in [[Design/Technology]] §Losing a technology (or a new paragraph
beside the tilled-plots section), and what I would recommend:

- **Reversion is the land's own clock, not an event.** Unworked fields slide back at the
  regrowth timescale the wood stock will use when it exists (~50-100 years to forest), which
  makes it the same rule as `R` recovering, one level up. A lapsed people's clearing is
  visibly still there for a generation and gone in three — which is also what the
  archaeology reads as abandonment.
- **The test is hands, not practice.** Fields beyond what the people can tend
  (`P * FARM_KM2_PER_PERSON`) revert whether or not the settlement still farms, so a
  halved village loses its outfields and keeps its infields. Practice lapsing is then just
  the case where the tended area is zero.
- **`meansPresent(TECH_FARMING)` becomes `sumTilled > 0`**, matching granaries. A farming
  people with no fields left are a people who have lost farming, which is the Tasmanian
  argument the decay model already makes for craft skills.

With those three lines written, the order is queueable as stated below.

## Recommended change

1. A `stepFields` in `population.h` beside `stepHeat`: tended area is
  `min(sumTilled, P * FARM_KM2_PER_PERSON)`; the excess reverts at the decided timescale,
  taking from the outermost site first (farmstead blocks before the village disc, highest
  slot first) so the clearing shrinks the way it grew.
2. `meansPresent(TECH_FARMING)` tests the fields; the grace period
  (`SKILL_GRACE_YEARS`, `technology.h:430`) already protects a settlement that has just
  taken farming up and not yet cleared anything.
3. `updateFarmland` is called after reversion so `farmK` and `tillSiteNext` follow, and a
  settlement that lost a site's fields can clear them again.
4. Reversion is not a work order and costs no labour: the wood takes it back by itself.

## Files

- `src/population.h` (new stage function, `Step`)
- `src/technology.h` (`meansPresent`)
- `src/farmland.h` (recompute after reversion)
- `src/settlement.h` (the reversion constant)
- `src/test_resources.cpp` (the scenario in Done when)
- `Design/Technology.md` (the design line, written by the developer before queueing)

## Done when

- `build_testresources.bat` builds and `build\test_resources.exe 7 700` runs.
- The probe prints tilled km2 held by settlements **not** practising farming. It is zero
  after the reversion timescale, and the run quotes the figure before and after.
- World tilled km2 at year 700 is within 15% of the pre-change 10,209 km2: reversion trims
  the abandoned edges, it does not stop farming.
- A forced-lapse scenario (a flag on the probe that lapses farming everywhere at year 300)
  shows tilled falling to zero over the decided timescale and not before.

## Depends on

Nothing in code. Blocked on the design line above.
