# 16 — One vector type, in double

**Status:** planned (2026-09-28); restated 2026-10-02 without line references

## Problem

Four small 3D vector types do one job, with hand conversions between them and three
functions that each turn a latitude and longitude into a direction. Positions of
settlements and bands are geometry held in float, against `standards/cpp.md` §Numbers.

Violates `standards/general.md` §Modules (a module hides one decision; four types for one)
and `standards/cpp.md` §Numbers (`double` for geometry).

## Evidence

- `terrain::V3` (float): about twenty files, noise, settlement and band positions, most of
  the simulation. `hydrology` uses it under the alias `V3orig`.
- `plates::V3` (float): a copy of `terrain::V3`.
- `camera::Vec3` (double): the camera, overlay, tooltip, `main.cpp`, `anchor.h`.
- `geodesic::D3` (double): the geodesic mesh and the atmosphere models on it; the only
  large arrays of vectors in the code (the mesh's and `qg2geo`'s), already double.
- Conversions by hand: `inspect::describePoint`, `main.cpp`'s `pickAt`,
  `World::terrainOffset`.
- Three latitude/longitude-to-direction functions: `camera::sphereDir`,
  `atmosphere::unitAt`, `hydrology::cellDir`.
- `standards/cpp.md` §Numbers names `terrain::V3` against `geodesic::D3` as the
  float/double split the code makes.

## Design

`standards/cpp.md` §Numbers and `standards/general.md` §Modules. Decided 2026-09-28:

- **One vector type, in double, for every vector on the CPU.** Per-cell field arrays are not
  vectors and stay float, as the standard says.
- **Rounding-level changes are accepted**, as for order 09: the probes are held to the world
  being the same, not to byte-identical files.

## Outcome

- One 3D vector type, double precision, used for every vector on the CPU: terrain and noise
  points, plate sampling, settlement and band positions, the camera, the mesh. The other
  three types are gone, with their conversions.
- One function turns a latitude and longitude into a direction; the other two are gone.
- Per-cell field arrays (heights, climate, suitability, the plate field) stay float.
- `standards/cpp.md` §Numbers says what the split now is: double for every vector and all
  geometry, float for per-cell fields.

## Files

A guide, not a limit: nearly every file in `src/` that names `V3`, `Vec3` or `D3` (about 25),
and `standards/cpp.md`.

## Done when

1. `build.bat` and every `build_*.bat` exit 0 with no warning beyond the existing C4996
   set.
2. `grep -nE "struct (V3|Vec3|D3)\b" src/*.h` finds one definition, and a grep for
   `sphereDir|unitAt|cellDir` finds one function (and its callers).
3. The climate is the same: the `CLIMATE` block and water line of
   `build\sweep.exe earth 0 rules 1` and `7 0 rules 1` are identical as printed to the
   pre-order run.
4. For the morning, not a gate: `build\test_resources.exe 7 40` and `3 40`, settlements
   and people before and after, quoted.
5. The world build is not slower: `World::build` for seed 7 and for `earth`, timed three
   times each before and after, is within 10% of the pre-order time. The Run quotes the
   times.
6. `build\test_savefile.exe 7 3` passes, and a save written before this order loads.
7. Screenshots (with `HH_BENCH=5`) at the five views of order 06's Done when: the Run quotes, per view, the
   share of pixels changed and the largest change, for the morning.

## Depends on

Nothing.
