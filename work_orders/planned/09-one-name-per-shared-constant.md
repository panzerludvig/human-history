# 09 — One name for each shared constant

**Status:** planned (2026-09-28) — split 2026-09-28: the vector type went to order 16, the
thresholds to 17, the unit names and enum classes to 18

Line references checked against 5087977 on 2026-09-28.

## Problem

The same constants are written out by hand across the code, in several spellings and under
several names, so two places can disagree about pi or the size of the Earth without anyone
noticing. Pi is a literal on 111 lines in four spellings and named six times; the Earth's
radius is a literal in fifteen files and 21 shader lines and named five times; the lapse
rate, "never" as a time and the herd factor repeat the same way.

Violates `standards/cpp.md` §Numbers ("physical constants are `constexpr` with the unit in
the name") and `standards/general.md` §Names carry units.

## Evidence

- **Pi.** `3.14159...`, `6.2831853` and `1.5707963` on 111 lines: 47 in
  `src/atmosphere.h`, 20 in `src/sweep.cpp`, 13 in `shaders/globe.frag`, the rest across
  17 other files. Named as `camera::PI` (`src/camera.h:32`), `daylight::PI_D`
  (`src/daylight.h:14`), `PI_F` in `src/hydrology.h:21` and `src/plates.h:17`, `PI` in
  `src/qg2geo.h:43` and `src/gridtest.cpp:13`, and `PI` in `globe.frag:63`.
- **Earth's radius.** Named as `camera::EARTH_RADIUS_KM` (`camera.h:33`), `EARTH_RADIUS_KM`
  in `hydrology.h:20` and `plates.h:18`, `R_EARTH` in metres (`atmosphere.h:27`) and
  `A_EARTH` (`qg2geo.h:40`); the literal `6371` in `sphere.h`, `bands.h`, `claims.h`,
  `farmland.h`, `terrain.h`, `population.h`, `settlement.h`, `sweep.cpp`, `transect.cpp`,
  `gridtest.cpp`, `solvetest.cpp` and 21 lines of `globe.frag`.
- **Lapse rate.** Named once as `PRE_LAPSE = 6.5` K/km (`atmosphere.h:725`); the literal
  `6.5` at `atmosphere.h:1986, 3020, 3030, 3049, 3070, 3107`, `terrain.h:345`
  (`temperatureC`) and `globe.frag:783, 799, 816, 834, 840, 906`.
- **Days per year.** Named once, `technology::YEAR` (`technology.h:12`); the literal `365`
  throughout `population.h`, `settlement.h`, `inspect.h`, `main.cpp` and `atmosphere.h`.
- **"Never" as a time.** `technology::INF_T = 1e18` (`technology.h:13`); the literal `1e18`
  at `settlement.h:641` and `bands.h:365, 705-706`, and `1e17` as "earlier than never" in
  `sim.h:141, 144, 152, 154, 162, 207`.
- **The herd factor.** `0.85f` applied to the herd at `population.h:363` (`foodTerms`),
  `population.h:626` (`driftAffinity`) and `bands.h:57` (`moverCap`), with no name saying
  what it is. (`FIELD_WORTH` and the `0.85f` inside `smoothstep` calls are other numbers.)

## Design

`standards/cpp.md` §Numbers and `standards/general.md` §Names carry units. No game design
changes.

Decided 2026-09-28: **rounding-level changes are accepted.** One value of pi in place of four
spellings moves numbers in the eighth digit; the probes are held to the world being the
same, not to byte-identical files.

## Outcome

- Pi, the Earth's radius, the lapse rate, days per year, "never" and the herd factor each
  have one definition, with the unit in the name where it has one, and every use in `src/`
  refers to it. Where a value is needed in another unit (the radius in metres), it is
  derived from the one definition.
- The shader uses the same definitions, received from the C++ side as order 06 arranges
  for its constants; no pi, radius or lapse literal is left in `globe.frag`.
- The comparisons against "never" test against the one name.

## Files

A guide, not a limit: a new header for the constants, or a block in the header at the root
of the include graph; `src/atmosphere.h`, `src/terrain.h`, `src/hydrology.h`,
`src/plates.h`, `src/daylight.h`, `src/camera.h`, `src/sphere.h`, `src/bands.h`,
`src/claims.h`, `src/farmland.h`, `src/population.h`, `src/settlement.h`,
`src/technology.h`, `src/sim.h`, `src/inspect.h`, `src/qg2geo.h`, `src/main.cpp`,
`src/sweep.cpp`, `src/transect.cpp`, `src/gridtest.cpp`, `src/solvetest.cpp`,
`shaders/globe.frag`.

## Done when

1. `build.bat` and every `build_*.bat` exit 0 with no warning beyond the existing C4996
   set.
2. `grep -nE "3\.14159|6\.2831853|1\.5707963|6371|1e1[78]" src/*.h src/*.cpp shaders/globe.frag`
   finds only the definitions. The Run section quotes the output.
3. The climate is the same: the `CLIMATE` block and water line of
   `build\sweep.exe earth 0 rules 1` and `build\sweep.exe 7 0 rules 1` are identical as
   printed to the pre-order run.
4. The world is the same to rounding: `build\test_resources.exe 7 40` and `3 40` give
   settlement counts and people within 1% of the pre-order run. The population model can
   amplify an eighth-digit change into a different founding somewhere, so exact equality
   is not required; the Run quotes both runs' totals and the diff.
5. Screenshots at the five views of order 06's Done when: the Run quotes, per view, the
   share of pixels that changed and the largest change. For the morning, not a gate.

## Depends on

06: the shader receives its constants from the C++ side only once 06 is on `main`.
