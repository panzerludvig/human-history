# 09 — One name for each shared constant

**Status:** implemented (2026-10-03): kept and merged into `main` as `6bafb81`

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

## Run

**Outcome:** passed. Branch `wo/09-one-name-per-shared-constant`, one commit `53f5394`.
Merged into `nightly` as `5d84618`, cleanly.

What it did: a new header, `src/constants.h` (namespace `constants`), defines `PI`,
`EARTH_RADIUS_KM`, `LAPSE_K_PER_KM`, `DAYS_PER_YEAR_INT` and `NEVER_DAY`, and derives the
float, metre and double forms from them. The six names for pi, five for the radius,
`atmosphere::PRE_LAPSE`, `technology::YEAR` and `technology::INF_T` are gone; every use
names `constants::`. Every 365 meaning a year uses the constant, 3650 and 36500 included.
The herd's factor is `population::HERD_SEASONAL_MEAN`. "Earlier than never" tests against
`NEVER_DAY`. `globeConstants` passes `PI`, `EARTH_RADIUS_KM`, `EARTH_RADIUS_M`,
`LAPSE_K_PER_KM` and `DAYS_PER_YEAR` to the shader, which writes none of them by hand.

Done when:

1. `build.bat` and all nine `build_*.bat` exit 0, C4996 only; again on `nightly`.
2. The grep finds only `src/constants.h:15` (`PI`), `:20` (`EARTH_RADIUS_KM`) and `:38`
   (`NEVER_DAY`), on the branch and on `nightly`.
3. `sweep.exe earth 0 rules 1` identical in full; `sweep.exe 7 0 rules 1` identical in the
   `CLIMATE` block and water line (one line elsewhere, `-82:+0.0` -> `-82:-0.0`); on
   `nightly` identical to the branch.
4. Quoted: `test_resources` at 40 years, settlements/people per pass, seed 7
   403/151905 and 406/150658 -> 400/151440 and 406/150658; seed 3 identical
   (399/158344, 401/157866). Screenshots, pixels changed and largest change:
   `46.318 -174.111 4 7` 0%, `46.318 -174.111 400 7` 0.0023% 1/255, `20 30 12000 7`
   0.0002% 1/255, `46.5 10 4 earth` 0.0001% 1/255, `46.5 10 400 earth` 0.0104% 1/255. On
   `nightly` all five byte-identical to the branch's, and `test_resources` identical but
   for its wall-clock line.

Files beyond the list: `anchor.h`, `overlay.h`, `world.h`, `news.h`; the probes
`gridtest.cpp`, `solvetest.cpp`, `test_qg2geo.cpp`, `test_savefile.cpp`, `terrprobe.cpp`,
`transect.cpp`; `Technical/Globe Viewer.md`, `Technical/Architecture.md`.

Unsure of: a new header rather than a block in an existing one. `gridtest`, `solvetest`
and `sweep` keep a local `R` derived from the radius. Rounding moves the order accepted:
the atmosphere's pi is full double; the shader's hut-cull radius squared, written as
40602000 and commented "6371^2", is the true 40589641; `1.5707963f` is `PI_F/2`; the
hut and farmstead angle hash uses `PI` for `3.14159`. For the game: huts and farmstead
dots at close zoom.

## Review

**Keep**, 2026-10-03. Merged into `main` (`6bafb81`); no issue found on the branch or on `nightly`. Huts and farmstead dots at close zoom are still to be seen in the game.

Review note for the night: [[Dev Log/Nightly/2026-10-02]].
