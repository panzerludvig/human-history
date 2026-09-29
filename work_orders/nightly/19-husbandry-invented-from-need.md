# 19 — Husbandry is invented from need, like farming

**Status:** taken by the night of 2026-09-28; passed, merged into `nightly/2026-09-28` as `63b6e54`

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

## Run

**Outcome:** passed. Branch `wo/19-husbandry-invented-from-need`, commits `5395216` (the
first-invention print) and `e9345d5`. Merged into `nightly` as `63b6e54`, last of the
night; `src/test_resources.cpp` conflicted only because 12 and 19 added functions at the
same place before `main`, both kept.

What it did: husbandry is in `needDriven`; its weight is sustained hunger times pasture
times `HUSB_NEED_SCALE` = 1.5, the runs behind it written next to the constant. The
order's premise was wrong: among hungry settlements pasture runs 0.78-0.97 of farm
suitability, and the unscaled weight 0.84-0.99 of farming's. Scale 1.0 gave husbandry
245 against farming 192 (1.27x); 1.5 gave 200.5 against 220.1.

Done when:

1. `build.bat` and `build_testresources.bat` exit 0, C4996 only.
2. `test_resources <seed> 700` prints `first invented (heat): husbandry  year 170` or
   `never` per technology and pass.
3. Seeds 1-10, 700 years, heat pass, farming / husbandry. Branch: 72/170, 274/308,
   158/37, 357/158, 293/239, 137/106, 225/213, 150/406, 116/102, 425/272; both 10/10;
   means 220.7 / 201.1 (0.91). On `nightly`, with 06-17 beneath it: 90/148, 418/32,
   271/173, 401/259, 179/122, 73/144, 218/111, 125/352, 265/390, 169/103; both 10/10;
   means 220.9 / 183.4 (0.83). Cold pass on `nightly`: 231.2 / 171.3 (0.74), 10/10.
4. Farming's mean, `main` -> branch: heat 204.3 -> 220.7, cold 200.1 -> 156.9;
   husbandry was never invented in the 20 pre-order runs.

Files beyond the list: `Technical/Globe Viewer.md` (it said husbandry stays on the
serendipity clock); one sentence of `Design/Technology.md` §Discovery beyond the table
row.

Unsure of: ten seeds cannot tell 1.0 from 1.5 (the spread of a 10-seed ratio is about
45%). The sentence replaced in the design note, "Pasture shares run far higher than farm
suitability", was the developer's prose. No technology uses the serendipity clock now.
`WorldState::nextEvent` and `fires` have 4 initializers for 5 technologies, harmless. For
the game: herders on the steppe a few centuries in.

Review note for the night: [[Dev Log/Nightly/2026-09-28]].
