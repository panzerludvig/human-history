# 04 — Split population.h and sim.h by concern

**Status:** queued (2026-09-15)

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
