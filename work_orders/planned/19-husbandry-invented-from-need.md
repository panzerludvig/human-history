# 19 — Husbandry is invented from need, like farming

**Status:** planned (2026-09-28)

Line references checked against 827c828 on 2026-09-28.

## Problem

Husbandry is the only technology still on the serendipity clock, with a world mean of
10,000 years, so it almost never appears inside a run: farming shows up within a few
centuries, herding practically never. History puts them side by side, as responses to the
same pressure, and [[Design/Technology]] now says so.

## Evidence

- `src/technology.h:15` — `INVENT_MEAN_YEARS = 10000.0`, the serendipity clock's mean.
- `src/technology.h:186-191` — `needDriven` lists farming and granaries; the comment says
  husbandry "stays on the serendipity clock".
- `src/technology.h:197-211` — `needWeight`: sustained hunger (`hungrySince`) ×
  `suitability` × `min(P/300, 3)`; `technology.h:129-131` — `suitability` is `sFarm` for
  farming and `pasture` for husbandry. Good farm sites are 0.05-0.16; pasture is a cover
  share up to 1, so the same formula unscaled would make husbandry several times faster
  than farming.
- `src/technology.h:258-292` — `scheduleInvention`: the need clock (`sqrt(sum) /
  NEED_MEAN_YEARS`) and the serendipity clock.
- No run recorded in the repo shows husbandry invented within its horizon
  (`Design/Resources.md`, "herd/person 0.00").

## Design

[[Design/Technology]] §Discovery, designed 2026-09-28: husbandry is need-driven like farming,
on the same sustained hunger, weighted by pasture, scaled so the world's first herders come
on about the same schedule as its first farmers. The calibration table there has the new
row.

## Outcome

- Husbandry is invented from need: a settlement's weight is its sustained hunger, as for
  farming, with its pasture in place of farm suitability, times a scale chosen so that
  husbandry's first invention in a world comes on about the same schedule as farming's.
  The scale is tuned by measurement (below) and the value and the runs behind it are
  written next to the constant.
- Nothing else about husbandry changes: adoption, the herd, the pasture cap and the loss
  of the skill are as they are.
- The calibration table in `Design/Technology.md` gets the measured mean for husbandry in
  place of "about farming's".

## Files

A guide, not a limit: `src/technology.h`, `src/test_resources.cpp` (the year of first
invention per technology), `Design/Technology.md` (the table).

## Done when

1. `build.bat` and `build_testresources.bat` exit 0 with no warning beyond the existing
   C4996 set.
2. `build\test_resources.exe <seed> 700` prints the year each technology was first invented
   in the world, or "never".
3. Over seeds 1 to 10 at 700 years: husbandry is invented in at least as many seeds as
   farming, and its mean year of first invention, over the seeds where both are invented,
   is within 30% of farming's. The Run section quotes the table: seed, farming's year,
   husbandry's year.
4. Farming's own mean over the same seeds is quoted before and after the order, to show it
   did not move beyond the noise of a changed random stream.

## Depends on

Nothing.
