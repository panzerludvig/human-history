# 03 — Take the superseded models out of the game build

**Status:** done (2026-09-15)

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
- Never referenced: `K_STORM` (584), `P_PER_DEG` (363), `FRICTION` (364),
  `DIV_CAP_SCALE` (574); `pAdv` (1388) only ever zeroed (2912); `sh` never incremented so `dbgN[2]`
  is always zero (2738-2751). The dead accumulators the order first listed were removed
  by order 02.
- `src/test_atmo.cpp` has no `build_*.bat`; its header gives a bare `cl` line.
- Line references checked against commit `9f598d3` on 2026-09-15, after orders 01 and 02
  landed.

## Design

`standards/general.md` §Superseded models and `standards/cpp.md` §Build. The branch map in
`Meta/Git.md` is the record every file that leaves `main` must appear in;
`Technical/Geodesic Grid.md` holds the deviation this order moves.

## Recommended change

1. Delete `atmosphere_geo.h`, `geosweep.cpp` and `build_geosweep.bat` from `main` and
  record their last state in the branch map in `Meta/Git.md` by commit hash, the way order
  01 recorded the rain path (`3e87389`). A run commits only on its own branch, so no
  reference branch is pushed to; the hash is the reference. Update
  `Technical/Geodesic Grid.md` to say so.
2. Decide per sub-model (DYN2, QG2, QG2GEO, WATER2) whether the sweep still needs it. Each
  that stays gets its include moved from `atmosphere.h` to `sweep.cpp` behind its own
  build, so the game no longer compiles it; each that does not stay is deleted with the same
  last-state hash in the branch map.
3. Delete the unreferenced constants and the dead `sh` counter.
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
- Every file that left `main` is named in the branch map with what it held and the hash of
  its last state.

## Depends on

01 (decided 2026-09-15: WATER2 has no live consumer; the mirror that read its rain is gone, and the `geow` sweep mode steps it for nothing).

## Run

**Status:** done (merged into `nightly/2026-09-15`, see below). Branch `wo/03-superseded-models`, forked from `4ac16a0`.

Commits, one per step:

- `98515c4` — step 1: `atmosphere_geo.h`, `geosweep.cpp`, `build_geosweep.bat` deleted; branch map and `Technical/Geodesic Grid.md` updated; Status set to in progress.
- `dd96e3c` — step 2: QG2GEO stays, in the sweep's build only (`sweep.cpp` defines `HH_QG2GEO` and includes `qg2geo.h` before `atmosphere.h`; seven `#ifdef HH_QG2GEO` sites in `atmosphere.h`). DYN2 (`dynamics2.h`), QG2 (`qg2.h`) and WATER2 (`water2geo.h`) deleted with their probes and build scripts (`test_dyn2.cpp`/`build_testdyn.bat`, `test_qg2.cpp`/`build_testqg.bat`, `dbg_qg2.cpp`/`build_dbgqg.bat`) and the sweep's `dyn`, `qg`, `geow` modes. Last state of all of it: `4ac16a0`, recorded in the branch map.
- `a197319` — step 3: `K_STORM`, `P_PER_DEG`, `FRICTION`, `DIV_CAP_SCALE`, `pAdv` and the `sh` counter (`dbgN[2]`) deleted.
- `38649f9` — step 4: `build_testatmo.bat` written; `test_atmo.cpp` header names it.

Why these per-model decisions: QG2GEO is the atmosphere the project is redoing on the mesh (`Technical/Geodesic Grid.md`, `Meta/Open Threads.md`), with `sweep.exe earth 0 geo 1` as its documented run line. QG2 is its lat-lon predecessor, which the note records as beaten (westerlies never below 51 degrees against 43 on the mesh); DYN2 is the lat-lon core QG2 replaced; both are on the grid being left behind (Dev Log 2026-09-15, order 01: "the physics that comes back comes back on the mesh"). WATER2 fed nothing since order 01, as this order's Depends on says.

Probe runs, all on the Earth template, all identical to a baseline captured from a sweep built at `4ac16a0` in the untouched tree (`build\baseline_sweep.txt`), compared with `fc` after steps 2 and 3 and at the final HEAD:

```
> build\sweep.exe earth 0 rules 1
spin-up 0 days
CLIMATE | err    7.1 | mean  14.9 rain 2.40 pRain  0.9 dry  20% cloud  45%
  Wv 26.78 mm, residence 11.1 d, wind  9.3 m/s, RH  69%, coast 2.05 inland 1.34
  water: evap  3.06 rain  2.40 mm/day
> fc build\baseline_sweep.txt build\final_sweep.txt
FC: no differences encountered
```

Done when, item 1 (`cl /showIncludes` on the `build.bat` line for `src\main.cpp`, output searched for `dynamics2.h|qg2.h|qg2geo.h|water2geo.h`):

```
cl exit=0
matches for the four sub-model headers: 0
src headers included: terrain.h plates.h hydrology.h population.h atmosphere.h daylight.h technology.h sim.h
```

Done when, item 2 (every `build_*.bat` at the root, exit code and warnings other than the pre-existing C4996):

```
build.bat: exit 0, non-C4996 warnings 0
build_gridtest.bat: exit 0, non-C4996 warnings 0
build_solvetest.bat: exit 0, non-C4996 warnings 0
build_sweep.bat: exit 0, non-C4996 warnings 0
build_terrprobe.bat: exit 0, non-C4996 warnings 0
build_testatmo.bat: exit 0, non-C4996 warnings 0
build_testqggeo.bat: exit 0, non-C4996 warnings 0
build_testresources.bat: exit 0, non-C4996 warnings 0
build_transect.bat: exit 0, non-C4996 warnings 0
```

Done when, item 3: the `main` row of the branch map in `Meta/Git.md` names every file that left, what it held and `4ac16a0` as its last state.

Files touched outside the Files section, all forced by the deletions: `src/test_dyn2.cpp`, `build_testdyn.bat`, `src/test_qg2.cpp`, `build_testqg.bat`, `src/dbg_qg2.cpp`, `build_dbgqg.bat` (the probes of the deleted headers; leaving them would have broken item 2). `sweep.cpp` changed more than includes: its `dyn`, `qg` and `geow` mode lines are gone and the dynamics printout's condition is `atmosphere::QG2GEO` alone, because the flags they set no longer exist. `build_sweep.bat` is unchanged: the define lives in `sweep.cpp` next to the include, so the guard and the header travel together.

Unsure of: `Meta/Open Threads.md` (2026-09-07 entry) still names `qg2.h`, `build_testqg.bat` and `test_qg2.exe` as a dated record; it is not in Files and was left alone. The sweep's "TWO-LEVEL DYNAMICS" table and the `d2*` field names in `Climatology` were kept as they are, since QG2GEO fills them; `d2psSd` is always zero there. `build_sweep.bat` and the other probe scripts do not create `build\` (only `build.bat` does), so a fresh checkout must run `build.bat` first or `mkdir build`; `build_testatmo.bat` creates it.

**Merged into `nightly/2026-09-15`** as `cc9e166`. Second probe on `nightly` after the
merge: `build.bat` and `build_sweep.bat` succeed; `build\sweep.exe earth 0 rules 1` prints
the same `CLIMATE` block, water line and rules report as the baseline (err 7.1, mean 14.9,
rain 2.40, pRain 0.9, dry 20%, cloud 45%; Wv 26.78 mm, residence 11.1 d, wind 9.3 m/s,
RH 69%, coast 2.05 inland 1.34; evap 3.06 rain 2.40 mm/day).
