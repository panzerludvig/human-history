# 08 — One world build shared by the game and every probe

**Status:** open (2026-09-15)

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
  column 311-335; contrast/transport 348-698; map moisture 705-746; height decomposition
  755-792; uplift 800-885; full-res cover 895-1046; raster 1050-1068; land cover and PPM
  writers 1073-1320; zonal profile 1330-1349; cloud 1358-1410.
- Re-implemented helpers: `landMaskedT` 202-223 is `atmosphere::bilinearAt` with a mask;
  coastal test at 77-83, 726-732, 1235-1241; `pet = max(0.4, 0.11*(t+8))` at 577, 603,
  717, 1095, 1205 (see `hydrology::petMmDay`, `main.cpp:1553`); PPM writer inlined at
  490, 492, 1248, 1287, 1290, 1292; lake flood-fill walked twice with a `W*H` visited
  vector per big lake at 172.

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
