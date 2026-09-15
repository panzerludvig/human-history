# 08 — One world build shared by the game and every probe

**Status:** done (2026-09-15)

## Problem

The seed-to-parameters derivation and the plates, sea level, hydrology, atmosphere chain
are copied into six executables. The sweep's `main` is about 1,282 lines of thirteen
brace-scoped reports, its header still describes a parameter sweep it no longer performs,
and it re-implements helpers that exist elsewhere.

Violates `standards/general.md` §Modules and `standards/cpp.md` §Shape.

## Evidence

- Derivation copies: `src/main.cpp:301-314` (`World::derive`), `src/sweep.cpp:132-145`,
  `src/geosweep.cpp:39`, `src/test_atmo.cpp:56`, `src/test_resources.cpp:84`,
  `src/transect.cpp:29`.
- Build-chain copies: `main.cpp:331-336` (`World::build`) and the same five probes.
- `sweep.cpp:1-4` header vs `sweep.cpp:272` "The sweep is no longer a sweep."
- Report blocks in `sweep.cpp`: lake census 151-195; land/sea vote 231-268; tropical
  column 317-341; contrast/transport 354-704; map moisture 711-752; height decomposition
  761-798; uplift 806-874; full-res cover 884-1035; raster 1039-1057; land cover and PPM
  writers 1062-1309; zonal profile 1319-1338; cloud 1347-1399.
- Re-implemented helpers: `landMaskedT` 202-223 is `atmosphere::bilinearAt` with a mask;
  coastal test at 77-83, 732-738, 1224-1230; `pet = max(0.4, 0.11*(t+8))` at 583, 609,
  723, 1084, 1194 (see `hydrology::petMmDay`, `main.cpp:1553`); PPM writer inlined at
  496, 498, 1237, 1276, 1279, 1281; lake flood-fill walked twice with a `W*H` visited
  vector per big lake at 172.
- Line references checked against commit `9f598d3` on 2026-09-15, after orders 01 and 02
  landed.

## Design

`standards/general.md` §Modules and §Verification (the probes are the test suite, so they
must build the same world the game does), `standards/cpp.md` §Shape. The probe list in
`Technical/Architecture.md` changes with it.

## Recommended change

1. After order 05 step 2, every probe includes `world.h` and calls `World::derive` and
  `World::build` with a null progress callback. Delete the six copies.
2. Split `sweep.cpp` into one function per report, each named after what it measures, and
  a `main` that picks reports by argument. Rename the file to what it is (a world census)
  or rewrite its header.
3. One PPM writer, one PET function, one coastal test, one bilinear-with-mask.

## Files

- `src/sweep.cpp` (or its renamed successor), `build_sweep.bat`
- `src/test_atmo.cpp`, `src/test_resources.cpp`, `src/transect.cpp`, `src/terrprobe.cpp`
  (derivation and build-chain copies only)
- `src/geosweep.cpp` if order 03 left it on `main`
- `src/world.h` (read only; created by order 05)
- `Technical/Architecture.md`

## Done when

- `grep -c "paramsFor" src/*.cpp` finds it called from `World::derive` only.
- `sweep.cpp` `main` is under 100 lines.
- Every probe's output is identical by seed before and after.

## Depends on

05 (step 2).

## Run

**Status:** done (pending merge). Branch `wo/08-one-world-build`, forked from `8c1d5e2`
(the head of `nightly/2026-09-15` after orders 03, 04 and 05).

Commits, one per step:

- `15cc915` step 1: `World::build` takes the stage to stop at (`world::Stage`: Plates,
  SeaLevel, Hydrology, Climate, Rivers, Settlements) and a null progress callback;
  `World::terrainOffset()` is the float offset the samplers take. `terrprobe` builds to the
  sea level, `test_atmo` to the hydrology, `sweep` and `transect` to the climate,
  `test_resources` through the reweighted rivers. The five derivation and build-chain
  copies are gone. `test_atmo` keeps its own `atmosphere::build(..., true)` call for the
  verbose flag (a monthly probe line on stderr the game never prints).
- `3a193d0` step 2: `sweep.cpp` is one static function per report (`reportLakes`,
  `reportLandSeaVote`, `reportScore`, `reportTropics`, `reportContrast`, `reportDynamics`,
  `reportWorldReview`, `reportNorthSplit`, `reportSurfaceZonal`, `reportMapMoisture`,
  `reportHeightSources`, `reportLift`, `reportCycle`, `reportFullRes`, `reportRainSpread`,
  `reportRaster`, `reportLandCover` (which prints `reportLandVsSea` and `reportPalette`
  between its table and its last line), `reportProfile`, `reportCloud`), a table in print
  order, and a `main` that sets the atmosphere's mode, builds the world, and runs every
  report or the comma-separated names in a fifth argument, refusing a name that is no
  report. The header was rewritten instead of renaming the file (the order allowed either):
  `sweep.exe` is the run line in `Technical/Climate Review.md`, `Technical/Globe Viewer.md`,
  `Technical/Geodesic Grid.md`, `Meta/Git.md`, `standards/general.md` and order 03's Run
  section, none of which are in this order's Files.
- `511a2e4` step 3: `writePpm` (six sites), `coastalCell` (three), `petMmDay` (five) are one
  function each in `sweep.cpp`; `atmosphere::bilinearCellAt` is the position-to-cell
  mapping that `bilinearAt` and the sweep's `landMaskedT` both call.

**Done when, checked** (after every step; the final numbers are from `511a2e4`):

1. `grep -n "paramsFor" src/*.cpp src/*.h`:

        src/terrain.h:49:inline ContinentParams paramsFor(float concentration) {
        src/world.h:102:        cp = terrain::paramsFor(concentration / 100.0f);

   Called from `World::derive` only. Passes.
2. `sweep.cpp` `main`: line 1638 of 1696, 59 lines. Passes.
3. Probe outputs identical by seed. Baseline captured from every probe built in the
   untouched tree at `8c1d5e2`; after each step every `build_*.bat` was rerun and the same
   six runs captured and compared with `fc /b` (29 files: the sweep's stderr and its ten
   PPM images for both seeds, `test_resources` stderr, `transect` stdout and stderr,
   `terrprobe` stdout, `test_atmo` stdout, stderr and nine BMPs):

        build\sweep.exe earth 0 rules 1     (from the repo root; 32,372 bytes of stderr)
        build\sweep.exe 7 0 rules 1         (19,885 bytes)
        build\test_resources.exe 7 40       (stderr, 24,980 bytes)
        build\transect.exe                  (10,913 + 427 bytes)
        build\terrprobe.exe                 (802 bytes)
        build\test_atmo.exe 7 build\atmo    (504 + 5,570 bytes, 9 BMPs of 55,350)

   Result after step 1, step 2 and step 3: `29 files compared, 0 differ`. Passes. The
   `CLIMATE` block is unchanged from order 03's baseline (earth: err 7.1, mean 14.9, rain
   2.40, pRain 0.9, dry 20%, cloud 45%; Wv 26.78 mm, residence 11.1 d, wind 9.3 m/s, RH 69%,
   coast 2.05 inland 1.34; evap 3.06 rain 2.40; seed 7: err 11.8, mean 14.3, rain 2.28, dry
   31%, cloud 44%). `test_resources 7 40` ends as order 04 recorded it (`wooded 376 /
   149174`, `0 settlements farm`).

Builds: every `build_*.bat` at the root exits 0 after every step, no warning other than
C4996. Counts moved: `build_terrprobe.bat` 2 to 3 (the `getenv` C4996 in `atmosphere.h`,
which terrprobe now includes through `world.h`), `build_sweep.bat` 10 to 5 (six `fopen`
sites became one).

Picker: `build\sweep.exe 7 0 rules 1 lakes,raster` prints the lake census and the raster
terrain block only; `build\sweep.exe 7 0 rules 1 nosuch` prints `no report named "nosuch";
the reports are lakes vote score ... cloud` and exits 1.

**Files beyond the order's list:**

- `src/world.h` (listed as read only): the stage argument and the null-safe progress are
  what lets a probe stop where its measurement starts; without them every probe would run
  the climate and the settlements to get a sea level, and `test_atmo`'s stderr would
  change. `build(progress)` for the game is unchanged (`main.cpp`, `savefile.h`,
  `test_savefile.cpp` untouched); the body is now `buildStages` with an early return after
  each stage.
- `src/atmosphere.h`: the one extraction `bilinearCellAt`, same float expressions in the
  same order; the probes that sample the climate (`test_resources`, the sweep's full-res
  report through `bilinearAt` itself) are byte-identical.

**Unsure of, for the morning:**

- **PET.** Step 3 says one PET function. Calling `hydrology::petMmDay` (float) from the
  sweep's double averages moves one byte of `map_seed7.ppm` (offset 0x5127, 0x8B to 0x8C;
  every other file identical). The sweep keeps a double twin, `petMmDay` in `sweep.cpp`,
  with the reason next to it. The renderer's PET is the float one, so the honest choice
  may be to take it and accept the pixel; that is a change of output, which this order
  forbade.
- **The angle constant.** The probes used `2 * 3.14159265358979` for the rotation angles;
  `World::derive` uses `2 * camera::PI` (`3.14159265358979323846`), a different double by
  seven ulps. The float rotation matrix came out identical for seeds 1 (earth) and 7, as
  the byte comparison shows; for some other seed the last bit of one entry could differ.
  The probes now build the game's world, which is the point.
- **Report order in the sweep.** The atmosphere now runs before the lake census and the
  land/sea vote (the world builds through the climate in one call), while the reports
  print in their old order. `atmosphere::build` prints nothing to stderr in the modes the
  sweep can reach (`verbose` is false, `PRESCRIBED` is never cleared), so the text is the
  same; only the `HH_DEBUG_PRE` lines, when that environment variable is set, would now
  come before `LAKES:` instead of after.
- `reportLandVsSea` and `reportPalette` are not in the picker's table; `landcover` prints
  them, in the old order, so a default run is unchanged.
- `test_atmo.cpp`'s `saveBmp` duplicates `bmp::write` (`src/bmp.h`) except for the
  top-down row order; outside step 3's list and left alone.
- The whole of `sweep.cpp` was clang-formatted in step 2 (every line moved into a
  function); steps 1 and 3 formatted changed lines only.
- The capture scripts and the baseline `test_atmo` built from `8c1d5e2` are under `build\`
  in the worktree (`probe_base`, `probe_step1..3`), untracked.
