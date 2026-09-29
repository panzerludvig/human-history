# 12 — Fields go back to the wild when nobody works them

**Status:** taken by the night of 2026-09-28; merged into `nightly/2026-09-28` as `44fed83`; Done when 6 missed on its branch and 3 missed on `nightly`, kept on the developer's instruction for the morning

Line references checked against 5087977 on 2026-09-28.

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

- `src/technology.h:434` — when practice lapses, `if (tech == TECH_HUSBANDRY) s.herd = 0;`
  is the only means cleared. `tilled[]` is untouched here and nowhere else.
- `src/technology.h:174-175` — `meansPresent` for `TECH_FARMING` returns `s.sFarm >= 0.05f`
  (the ground); `TECH_GRANARY` on the next lines returns `s.granaries > 0 || s.buildWork >
  0` (the built thing).
- `src/farmland.h:64-75` — `updateFarmland` computes `farmK` from `tilled[]` with no
  reference to practice or to hands; `technology.h:151` multiplies it by expertise, which
  is zero for a lapsed people, so the yield is correctly zero while lapsed and returns in
  full on re-adoption. The stock, not the flow, is what is wrong.
- `src/settlement.h:660` — `float tilled[1 + FSTEAD_MAX]`, written only by
  `population.h:588` (the clearing work order, in `stepBuilding`) and the save loader
  (`savefile.h:191`; saved at 67; save format version 24 at 42).
- `src/technology.h:425-430` — a practice at the skill floor lapses after
  `SKILL_GRACE_YEARS` (40, `settlement.h:505`) without its means.
- Measured 2026-09-18: `build\test_resources.exe 7 700` reported 10,209 km2 tilled with 480
  settlements farming; the 2026-08-31 decay measurement had 522 settlements lapse from
  farming along the way, every one of which still holds its fields.

## Design

[[Design/Technology]], designed 2026-09-28: "Fields go back to the wild" at the end of the
farming section, and the farming case of "The means, not the share" under Losing a
technology.

## Outcome

- **What nobody can tend reverts.** Tilled land beyond what the settlement's people can
  work (`FARM_KM2_PER_PERSON` a person) reverts, whether or not the settlement still
  farms. A settlement whose farming has lapsed tends nothing, so all its land reverts.
- **At the land's pace.** Reversion costs no labour and runs steadily: a block nobody works
  is gone about 50 years after the last hand left it, and not much sooner.
- **Outermost first.** Farmstead blocks revert before the village's own fields, the
  farmstead farthest from the village first. (Today's spiral puts later farmsteads
  farther out, so this is close to newest first until order 13 replaces the spiral.) Land that has reverted can be cleared again at the full
  cost, and the settlement's choice of the next plot and its farm food follow the land
  that stands.
- **Fields are farming's means.** Farming's means are present while fields stand or a plot
  is being cleared. A settlement that has never had a field is judged on the ground, as
  today, so a farming people is never lost for not yet having cleared anything, however
  long it takes to need a field. Once a settlement has had fields, losing them all is
  losing the means.
- **Saves keep working.** Whatever the settlement now remembers about having had fields is
  saved; a save from before this order loads, counting a settlement with any tilled land
  as having had fields.
- The map needs no change: it draws the plots that stand, so it shows the reversion.
- `Technical/Globe Viewer.md` (the farmland paragraph) says fields revert.

## Files

A guide, not a limit:

- `src/population.h` (a stage for the reversion; `Step`)
- `src/technology.h` (`meansPresent`)
- `src/farmland.h` (following the land that stands)
- `src/settlement.h` (the reversion constant; what the settlement remembers)
- `src/savefile.h` (the new field; a version step)
- `src/test_resources.cpp` (the checks in Done when)
- `Technical/Globe Viewer.md`

## Done when

1. `build.bat`, `build_testresources.bat` and `build_testsavefile.bat` exit 0 with no
   warning beyond the existing C4996 set.
2. `build\test_resources.exe 7 700` and `3 700` print, at the end, the tilled km2 held by
   settlements not practising farming, split into those that lapsed in the last 50 years
   and those that lapsed earlier. **The second figure is zero.**
3. The same runs print the number of farming lapses for want of means by settlements that
   had never had a field. **It is zero.**
4. A forced-lapse check (a probe option that ends farming everywhere at year 300): world
   tilled km2 at years 300, 310, 325, 350 and 360 are quoted; it is still above 60% of the
   year-300 figure at 310 and **zero by 360**.
5. `build\test_savefile.exe 7 3` passes, and a save written before this order loads.
6. A sanity guard, not a target: settlements practising farming at year 700 are within 15%
   of the pre-order run for the same seed, and world tilled km2 is not below half of it.
   The Run quotes both runs' figures (farming settlements, tilled km2, lapses).

## Depends on

Nothing.

## Run

**Outcome:** failed by the letter on its branch (Done when 6) and on `nightly` (Done
when 3); kept on `nightly` on the developer's instruction, for the morning. Branch
`wo/12-fields-go-back-to-the-wild`, one commit `6ff29a5`. Merged into `nightly` as
`44fed83`, after 11.

The merge conflicted with 11 in three files, both orders judged independent:
`src/test_resources.cpp`, four hunks of each order's own report lines, both kept;
`src/settlement.h`, 11's comment on `TILL_LABOUR_SHARE` then 12's lines appended after it;
`src/population.h`, the comment above `advance`, which both rewrote. The merged comment
names both stages and is a line neither order wrote: a deviation from the conflict rule,
made on the developer's instruction.

What it did: `population::stepReversion`, after `stepBuilding`: each person tends
`FARM_KM2_PER_PERSON`, nobody tends once farming has lapsed, hands go to the village's
plots first and then outward; untended land reverts at no labour cost at a pace
(`Settlement::wildPace`) that clears it `FIELD_REVERT_YEARS` (50) after the last hand
left. `technology::meansPresent` for farming is fields standing or a plot being cleared
once `Settlement::hadFields` is set, the ground before that. Save format v25 adds both
fields; an older save flags every settlement holding land.

Done when, branch / `nightly`:

1. The three builds exit 0, C4996 only.
2. Tilled km2 held by settlements not farming, lapsed more than 50 years ago: 0.0 on
   both seeds and passes, on the branch and on `nightly`.
3. Farming lapses for want of means by settlements that never had a field: 0 on the
   branch; on `nightly` 6 (seed 7 cold) and 5 (seed 3 cold), 0 in the heat passes; after
   17 merged, 12/8 and 1/1. A settlement that never had a field is judged on the ground,
   as the design says, so a farming people on unfarmable ground still lapses; the check
   counts those too. Whether the check or the design is wrong is for the morning.
4. `test_resources 7 360 lapse`, heat pass, branch: y300 229.6, y310 183.6, y325 114.8,
   y350 0.0, y360 0.0. On `nightly`: cold 143.2, 114.6, 70.4, 0.0, 0.0; heat 54.0, 43.2,
   27.0, 0.0, 0.0.
5. `test_savefile 7 3`: 70174 fields, 0 mismatches, branch and `nightly`. A v24 save
   (seed 3, year 700) loads on the branch with 460 flagged as having had fields; on
   `nightly` both pre-order saves load in the game.
6. Farming settlements / tilled km2 / lapses at year 700, pre-order -> order: seed 7
   cold 163/3097/0 -> 87/1529/0, heat 207/3884/0 -> 250/9220/0; seed 3 cold 81/1669/0 ->
   95/2179/0, heat 334/8679/2 -> 287/6429/0. Outside the 15% band in 3 of 4. With the old
   code, changing only the technology random seed moves the same figure 3-12x; the
   developer ruled the guard noise.

Files beyond the list: `src/test_savefile.cpp`.

Unsure of: untended blocks revert side by side, each over 50 years from when its last
hand left, not one after another. The Evidence's "522 lapses" came from `lostT`, which
also moves while neighbours practise; the probe now counts real lapses, which are rare.
A settlement that relocates starts with `hadFields` false. For the game: a lapsed
village's plots thinning over about 50 years; loading an old save.

Review note for the night: [[Dev Log/Nightly/2026-09-28]].
