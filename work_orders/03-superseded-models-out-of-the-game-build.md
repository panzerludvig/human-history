# 03 — Take the superseded models out of the game build

**Status:** open (2026-09-15)

## Problem

`atmosphere.h` includes `dynamics2.h`, `qg2.h`, `qg2geo.h` and `water2geo.h`, so the sigma
core, both quasi-geostrophic models and the two-layer water are compiled into
`humanhistory.exe` and every probe, all off by flag. `atmosphere_geo.h` references seven
constants that no longer exist in `atmosphere.h` and cannot compile, yet
`build_geosweep.bat` exists for it. Unused constants and never-written state sit beside
the live code.

Violates `standards/general.md` §Superseded models (buildable on `main` or deleted;
reference code lives on a named branch in the branch map) and `standards/cpp.md` §Build
(every executable has a build script that works).

## Evidence

- `src/atmosphere.h:12-15` — the four includes.
- `src/atmosphere_geo.h:212-283` — references `A::K_ICE_COND`, `RHO_UPPER`, `H_UPPER`,
  `MELT_DAMP`, `LAPSE_OFFSET`, `W_SURFACE`, `RAIN_RATE`; none defined in `atmosphere.h`
  (verified by grep 2026-09-15).
- `build_geosweep.bat`, `src/geosweep.cpp` — the only consumer of `atmosphere_geo.h`.
- `Technical/Geodesic Grid.md:11` — records the geo port as "kept as the worked example
  ... not as code to build", a deviation written in the wrong place.
- Never referenced: `K_STORM` (605), `P_PER_DEG` (363), `FRICTION` (364),
  `DIV_CAP_SCALE` (594); `pAdv` (1423) never written; `madeAcc += 0; rainAcc += 0;`
  (2834-2835); `sh` never incremented so `dbgN[2]` is always zero (2893-2901).
- `src/test_atmo.cpp` has no `build_*.bat`; its header gives a bare `cl` line.

## Design

`standards/general.md` §Superseded models and `standards/cpp.md` §Build. The branch map in
`Meta/Git.md` is the record every file that leaves `main` must appear in;
`Technical/Geodesic Grid.md` holds the deviation this order moves.

## Recommended change

1. Move `atmosphere_geo.h`, `geosweep.cpp` and `build_geosweep.bat` to a reference branch
  (the existing `climate-wind` is where that port was born) and add the line to the branch
  map in `Meta/Git.md`. Update `Technical/Geodesic Grid.md` to point at the branch.
2. Decide per sub-model (DYN2, QG2, QG2GEO, WATER2) whether the sweep still needs it. Each
  that stays gets its include moved from `atmosphere.h` to `sweep.cpp` behind its own
  build, so the game no longer compiles it; each that does not stay goes to the reference
  branch with the same one-line branch-map entry.
3. Delete the unreferenced constants and the dead accumulators.
4. Either write `build_testatmo.bat` or delete `test_atmo.cpp`.

## Files

- `src/atmosphere.h` (the four includes, the dead constants and accumulators)
- `src/atmosphere_geo.h`, `src/geosweep.cpp`, `build_geosweep.bat` (leave `main`)
- `src/dynamics2.h`, `src/qg2.h`, `src/qg2geo.h`, `src/water2geo.h` (each stays behind its
  own build in `src/sweep.cpp` or leaves `main`)
- `src/sweep.cpp` (includes only), `build_sweep.bat`
- `src/test_atmo.cpp` and a new `build_testatmo.bat`, or the file is deleted
- `Meta/Git.md` (branch map), `Technical/Geodesic Grid.md`

## Done when

- `humanhistory.exe` builds without including any of the four sub-model headers
  (check: `cl /showIncludes`).
- Every `build_*.bat` at the root succeeds.
- Every file that left `main` is named in the branch map with what it holds.

## Depends on

01 (decided 2026-09-15: WATER2 has no live consumer; the mirror that read its rain is gone, and the `geow` sweep mode steps it for nothing).
