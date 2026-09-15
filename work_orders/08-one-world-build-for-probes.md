# 08 — One world build shared by the game and every probe

**Status:** queued (2026-09-15)

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
