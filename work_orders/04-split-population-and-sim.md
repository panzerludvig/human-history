# 04 — Split population.h and sim.h by concern

**Status:** done (2026-09-15)

## Problem

`sim.h` is the population model and `population.h` is its constants table plus one
integrator. The 27 function-local `using namespace population;` lines in `sim.h` and
`technology.h` all pull constants and enum values, not behaviour, which shows the split is
by file size rather than by decision. `sim.h` writes settlement internals directly, the
effective-food formula exists at four sites, and `Settlement` is constructed positionally
in two files so members can only be appended.

Violates `standards/general.md` §Modules (one header per concern; a module hides one
decision) and `standards/cpp.md` §Types and ownership (no `using namespace` in headers)
and §Shape (`advance` is 241 lines; its seven blocks are already functions in shape).

## Evidence

- Content coupling: `applyClaim` `src/sim.h:202-213` writes `s.kFoodP/kGame/...` with the
  same scaling `population::build` does at `population.h:1043-1053`; `updateFarmland`
  `sim.h:624-647` fills a cache `advance` reads at `population.h:1319-1324, 1356` (the
  comment at 638 admits it); `foundSettlement` `sim.h:827` and `build` `population.h:1022`
  both construct `Settlement` positionally (see the comment at 609).
- Four copies of "what this settlement eats": `technology::effectiveK` 142-158,
  `population::foodFlow` 1173-1202, `advance` 1424-1430, `gameTick` `sim.h:518-524`.
- `advance` `population.h:1208-1448`: heat ledger 1254-1283; annual fill 1299-1328; three
  build clocks of identical shape 1332, 1342, 1353; bows 1365-1375; herd 1376-1381;
  affinity 1388-1400; horizon 1421-1446; twenty fields shadow-copied to locals 1212-1225
  and written back 1401-1419.
- Dead forward declarations after their definitions: `sim.h:395`, `sim.h:622`.
- Event kinds `0..4` as bare ints with an if-chain: `sim.h:1432-1520`.
- Erase-in-loop on `pf.bands` at `sim.h:983, 1002, 1021, 1106` and linear id scans at
  `sim.h:1522`, `population.h:740`.
- Lat-lon grid coupling is confined to six functions: `cellOf` `sim.h:43`, `gameRegion`
  `population.h:750`, `claimant` `sim.h:145`, `roomKm` `sim.h:168`, `bestProspect`
  `sim.h:418`, and `build`'s stencils `population.h:887-1008`. Everything else works on
  unit vectors.
- Line references checked against commit `9f598d3` on 2026-09-15, after orders 01 and 02
  landed.

## Design

`standards/general.md` §Modules, `standards/cpp.md` §Types and ownership and §Shape. The
model being split is the one in `Design/Population.md`; this order changes its layout, not
its rules.

## Recommended change

Split first, then dedupe; no behaviour change until the last step, verified by
`test_resources.exe` output being identical by seed.

1. `settlement.h`: the constants, `Settlement`, `Band`, `Field`, `Cohorts`, `SeasonCtx`.
  Give `Settlement` a named constructor so both founding sites use it.
2. `population.h` keeps `build` and `advance`; `advance`'s seven blocks become functions
  over a small per-step struct, the three build clocks one function called three times.
3. `claims.h`, `bands.h`, `raids.h`, `farmland.h`, `events.h` carved from `sim.h`; `sim.h`
  keeps the queue and `simulate`. Event kinds become an `enum class` with an exhaustive
  switch.
4. One `effectiveFood` function; the other three sites call it.
5. Remove every `using namespace`; spell the namespace.

The six grid-coupled functions are left alone here and named in a later geodesic
migration order.

## Files

- `src/sim.h`, `src/population.h`, `src/technology.h`
- New: `src/settlement.h`, `src/claims.h`, `src/bands.h`, `src/raids.h`, `src/farmland.h`,
  `src/events.h`
- `sim.h` keeps including the carved headers, so no include block outside these files
  changes. That is what keeps this order off `main.cpp`, which order 05 owns.

## Done when

- No `using namespace` in any header.
- `test_resources.exe <seed> <years>` prints identical output before and after for two
  seeds.
- No header under `src/` needs "and" to describe it in its opening comment.

## Depends on

Nothing. Independent of 01-03.

## Run

**done** (merged into `nightly/2026-09-15`, see below), 2026-09-15. Branch `wo/04-split-population`, forked
from `4ac16a0`. One commit per step, in order:

- `07bfdfa` step 1: `settlement.h` carved out of `population.h`; `newSettlement`
  replaces both positional initialisers.
- `ec259ab` step 2: `advance` over `population::Step`; seven step functions; one
  `workClock` for the three build clocks.
- `a41e4d5` step 3: `sim.h` carved into `sphere.h`, `events.h`, `claims.h`,
  `farmland.h`, `raids.h`, `bands.h`; queue kinds are `enum class Due` with an
  exhaustive switch; `decaySkills` to `technology.h`.
- `60f35a6` step 4: `population::foodTerms` / `effectiveFood`, `technology::annualCtx`;
  the four sites assemble from the terms.
- `b3589d5` step 5: the 27 `using namespace population;` lines removed, names
  qualified; header descriptions reworded; two vault notes updated.

Probe. Baseline from the untouched tree at `4ac16a0`, then after every step:

    build_testresources.bat
    build\test_resources.exe 7 40  > build\<tag>_7_40.txt 2> build\<tag>_7_40.err
    build\test_resources.exe 3 40  > build\<tag>_3_40.txt 2> build\<tag>_3_40.err

The probe writes everything to stderr (stdout is empty for both seeds). Result after
each of the five steps, and after the final commit: **identical** to the baseline,
byte for byte (SHA-256 of the files compared), for both seeds. Last five lines of each:

    seed 7:
      wooded (>50%)      377    148930   0.9%

    baseline by wood cover: treeless 0 /        0, sparse 26 /     1873, wooded 376 /   149174
    baseline: 0 settlements farm (0 people); 0 km2 tilled, 0 plots being cleared; fields feed 0; 0 farmsteads stand, 0 going up
    heat    : 0 settlements farm (0 people); 0 km2 tilled, 0 plots being cleared; fields feed 0; 0 farmsteads stand, 0 going up

    seed 3:
      wooded (>50%)      394    157614   0.8%

    baseline by wood cover: treeless 0 /        0, sparse 6 /     1141, wooded 395 /   157400
    baseline: 0 settlements farm (0 people); 0 km2 tilled, 0 plots being cleared; fields feed 0; 0 farmsteads stand, 0 going up
    heat    : 0 settlements farm (0 people); 0 km2 tilled, 0 plots being cleared; fields feed 0; 0 farmsteads stand, 0 going up

`grep -rn "using namespace" src/*.h` prints nothing. `build.bat` and
`build_testresources.bat` succeed after every step with the same seven pre-existing
C4996 warnings (fopen, getenv) and no new ones.

Header descriptions: the first sentence of every header this order created or touched
(`settlement.h`, `population.h`, `technology.h`, `sim.h`, `sphere.h`, `events.h`,
`claims.h`, `farmland.h`, `raids.h`, `bands.h`) contains no "and"; that is the
criterion applied.

Unsure of, for the morning:

- **One file beyond the list:** `src/sphere.h` (cellCentre, cellOf, distKm, norm3,
  moveToward). Every carved header uses them and they belong to none of them.
- **Namespaces:** the carved headers stay in `namespace sim` and `settlement.h` in
  `namespace population`, because `main.cpp` (order 05) qualifies their names
  (`sim::cellCentre`, `population::Settlement`, ...). One namespace per header holds;
  a namespace now spans several headers.
- **Step 4 is not quite "one function"** at the fourth site. `sim::gameTick` takes
  every term from `foodTerms` except the herds' flow, which it multiplies in its old
  order `(kGame * huntEff) * bows * meanF`; `f.bigGame` groups the same factors as
  `kGame * (huntEff * bows)`, and that rounding moved the seed-7 probe by one person
  (people 151047 -> 151048). Written next to the code as a deviation. Making the other
  three sites match gameTick instead would push the rounding into `K` and every
  settlement's R integration, so it was not tried.
- **`decaySkills` moved to `technology.h`**, which therefore includes `events.h`
  (`sim::note`) — a lower header naming a `sim` function. `gameTick` stays in `sim.h`
  for the same reason in reverse.
- **Two notes touched outside the Files list:** `Technical/Globe Viewer.md` (two
  phrases) and `Design/Borders.md` (one sentence), so every path they name exists.
- **Line endings:** `sim.h` had a UTF-8 BOM and CRLF in the working copy; the new
  files are LF without BOM (the index is LF for all headers under `text=auto`).
- Order 05's line references into `sim.h` and `population.h` are stale after this.

**Merged into `nightly/2026-09-15`** as `0d245e2`, after order 03. Second probe on
`nightly`: `build.bat` and `build_testresources.bat` succeed; `build\test_resources.exe 7 40`
and `3 40` stderr byte-identical to the `4ac16a0` baseline (`fc /b`: no differences;
24980 and 7729 bytes). The probe writes only to stderr; capturing it through PowerShell's
`2>` wraps lines and breaks the comparison, so capture through `cmd /c`.
