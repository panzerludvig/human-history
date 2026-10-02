# 09 — One name for each shared constant

**Status:** planned (2026-09-28) — split 2026-09-28: the vector type went to order 16, the
thresholds to 17, the unit names and enum classes to 18; restated 2026-10-02 without line
references, after order 06

## Problem

The same constants are written out by hand across the code, in several spellings and under
several names, so two places can disagree about pi or the size of the Earth without anyone
noticing. Pi is a literal in about twenty source files and the shader, in several
spellings, and is named six times; the Earth's radius is a literal in most of the
simulation's headers and on some twenty lines of the shader, and is named five times; the
lapse rate, days per year, "never" as a time and the herd's seasonal factor repeat the same
way.

Violates `standards/cpp.md` §Numbers ("physical constants are `constexpr` with the unit in
the name") and `standards/general.md` §Names carry units.

## Evidence

- **Pi**: `3.14159...`, `6.2831853` and `1.5707963` throughout, most of them in
  `atmosphere.h`, `sweep.cpp` and `globe.frag`. Named as `camera::PI`, `daylight::PI_D`,
  `PI_F` in both `hydrology` and `plates`, `PI` in `qg2geo`, and `PI` in the shader, which
  order 06 left written by hand.
- **The Earth's radius**: named as `camera::EARTH_RADIUS_KM`, `EARTH_RADIUS_KM` in both
  `hydrology` and `plates`, `atmosphere::R_EARTH` and `qg2geo`'s `A_EARTH` in metres; the
  literal `6371` elsewhere in the simulation, the probes and the shader.
- **The lapse rate**: named once, `atmosphere::PRE_LAPSE` (6.5 K/km), and written as
  `6.5` in the rest of the atmosphere, `terrain::temperatureC` and the shader.
- **Days per year**: named once, `technology::YEAR`; `365` written out across the
  population model, the tooltip, `main.cpp` and the atmosphere.
- **"Never" as a time**: `technology::INF_T` (`1e18`), and the literal `1e18` in the
  settlements and bands, and `1e17` as "earlier than never" in the event queue.
- **The herd's seasonal factor**: `0.85f`, the livestock's seasonal mean flow, applied to
  the herd in `population::foodTerms`, `population::driftAffinity` and the bands' mover
  capacity, with no name saying what it is. Other `0.85` values (inside `smoothstep`
  calls, `FIELD_WORTH`) are different numbers.

## Design

`standards/cpp.md` §Numbers and `standards/general.md` §Names carry units. No game design
changes.

Decided 2026-09-28: **rounding-level changes are accepted.** One value of pi in place of
several spellings moves numbers in the eighth digit; the probes hold the world to being
the same, not to byte-identical files.

## Outcome

- Pi, the Earth's radius, the lapse rate, days per year, "never" and the herd's seasonal
  factor each have one definition, with the unit in the name where it has one, and every
  use in `src/` refers to it. Where a value is needed in another unit (the radius in
  metres), it is derived from the one definition.
- The shader uses the same definitions, received from the C++ side the way order 06
  passes its other constants; no pi, radius or lapse literal is left in `globe.frag`.
- The comparisons against "never" test against the one name.

## Files

A guide, not a limit: a new header for the constants, or a block in the header at the root
of the include graph; most of `src/` (`atmosphere.h`, `terrain.h`, `hydrology.h`,
`plates.h`, `daylight.h`, `camera.h`, `sphere.h`, `bands.h`, `claims.h`, `farmland.h`,
`population.h`, `settlement.h`, `technology.h`, `sim.h`, `events.h`, `inspect.h`,
`qg2geo.h`, `main.cpp`, the probes) and `shaders/globe.frag`.

## Done when

1. `build.bat` and every `build_*.bat` exit 0 with no warning beyond the existing C4996
   set.
2. `grep -nE "3\.14159|6\.2831853|1\.5707963|6371|1e1[78]" src/*.h src/*.cpp shaders/globe.frag`
   finds only the definitions. The Run section quotes the output.
3. The climate is the same: the `CLIMATE` block and water line of
   `build\sweep.exe earth 0 rules 1` and `build\sweep.exe 7 0 rules 1` are identical as
   printed to the pre-order run.
4. For the morning, not a gate: `build\test_resources.exe 7 40` and `3 40`, settlements
   and people before and after, quoted; and the share of pixels changed and the largest
   change in screenshots at `46.318 -174.111 4 7 30 60 0 0`,
   `46.318 -174.111 400 7 30 60 0 0`, `20 30 12000 7 30 60 0 0`,
   `46.5 10 4 earth 30 60 0 0.0012937` and `46.5 10 400 earth 30 60 0 0.0012937`
   (with `HH_BENCH=5`, so the game exits after the shot).

## Depends on

06 (implemented): the shader receives its constants from the C++ side.
