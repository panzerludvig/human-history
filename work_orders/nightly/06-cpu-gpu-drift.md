# 06 — Close the CPU/GPU drifts and mark every mirror

**Status:** planned (2026-09-28)

Line references checked against 5087977 on 2026-09-28.

## Problem

Three mirrored rules have drifted, one mirror comment names a function that no longer
exists, most mirror comments are one-sided (the shader names the CPU, the CPU rarely names
the shader), some mirrored pairs carry no comment on either side, two shader functions are
dead mirrors with no callers, and fourteen constants are defined by hand in both C++ and
GLSL.

Violates `standards/general.md` §Mirrored code.

## Evidence

- **Lake shore.** The tooltip (`src/inspect.h:194-211`, in `inspect::describePoint`) takes
  the highest lake level of the 3x3 cells around the point; the shader
  (`shaders/globe.frag:364` `lakeLevelAt`) takes a smoothstep-weighted average of the 2x2
  cells whose centres surround it. Both then add 12 m (`inspect.h:205`,
  `globe.frag:1136` in `main`). The average sits below the maximum near a lake's edge, so
  the tooltip calls ground "Lake" that the globe draws as land. The comment at
  `inspect.h:108` still says "same rule as the shader".
- **Ice.** The sim freezes water at a hard `sim::FROZEN_T` of -2 C (`src/bands.h:20`): bands
  walk on it at full speed (`bands.h:470`), frozen sea counts as drinkable
  (`bands.h:197`), and the tooltip says "(frozen)" (`inspect.h:102, 206`). The shader draws
  ice as a ramp, `smoothstep(-1.0, -4.0, tLoc)` (`globe.frag:835`, `iceAt`), half-formed
  at -2.5 C. Bands therefore walk across ice the globe draws as barely forming.
- **Diurnal peak.** The tooltip's current temperature peaks at 14:00 (`inspect.h:70, 79`,
  `describePoint`); the atmosphere model the game runs peaks at 15:00
  (`src/atmosphere.h:925`, `Prescribed::surfaceT`). Both are CPU, but the tooltip
  re-derives the swing instead of asking the model.
- **Stale name.** `src/farmland.h:16` (over `sim::granaryPos`) cites `granaryNear` in the
  shader; it is now `hutsNear` (`globe.frag:476-566`).
- **The season interpolation** `fmod(t, 365) / 365 * 4 - 0.5` is written out at
  `inspect.h:88` (`describePoint`), `atmosphere.h:2997` (`seasonalAt`),
  `src/settlement.h:873` (`population::cachedSeasonT`) and `globe.frag:758`
  (`climSample`). The shader's is a mirror; the two extra CPU copies are not.
- **Dead mirrors.** `globe.frag:905` `temperatureC` and `:910` `moistureAt` have no callers
  in the shader. (Their CPU originals stay: they are the atmosphere's first-guess
  bootstrap.)
- **Constants defined on both sides by hand**, CPU first:
  `hydrology::W/H` (`hydrology.h:18`) and `HW/HH` (`globe.frag:60`);
  `hydrology::NO_LAKE` (`hydrology.h:19`, `globe.frag:61`);
  `plates::W/H` (`plates.h:16`) and `PW/PH` (`globe.frag:224`);
  `terrain::HEIGHT_SCALE_M`, `CRUST_WEIGHT`, `LAND_RELIEF`, `RANGE_GAIN`
  (`terrain.h:18, 20, 26, 30`; `globe.frag:56, 53, 58, 59`);
  `terrain::NSUB/NCOV` (`terrain.h:356-357`, `globe.frag:921-922`);
  `textures::SITE_STRIDE` (`textures.h:84`, `globe.frag:392`);
  `overlay::HUT_KMPP/WALK_KMPP` (`overlay.h:85-86`, `globe.frag:464-465`).
  `LAND_RELIEF` and `RANGE_GAIN` are `inline float`, reassigned only by `terrprobe.cpp`,
  which renders nothing.
- **Mirror comments.** Present on the shader side for the noise stack, the terrain,
  `climFuzz`, the mixtures and the settlement drawing; absent or one-sided on the CPU side
  for most of them. No comment on either side for `atmosphere::bilinearAt/annualAt/
  seasonalAt` (`atmosphere.h:2978-3001`) against `climSample/climAnnual`
  (`globe.frag:753-778`), or `sim::cellCentre` (`src/sphere.h:12`) against
  `cellCentre` (`globe.frag:381`).

## Design

`standards/general.md` §Mirrored code (the CPU is the source of truth; the shader is the
copy; both sites name each other). The ice rule is `Design/Migration.md`, passability,
decided 2026-09-28: bands walk only on fully formed ice. The lake and diurnal rules are
decided here, as below, and recorded in `Technical/Globe Viewer.md` by this order.

Decided 2026-09-28, and stated as outcomes below: the lake shore follows the shader's
smooth average; bands cross only fully formed ice, at -4 C, the cold end of the drawn
ramp; the daily temperature peaks at 15:00, as the atmosphere model has it.

## Outcome

- **One lake-shore rule.** The tooltip and the globe decide where a lake's shore is by the
  same rule, the shader's smooth average, and its CPU copy is the source of truth.
- **One ice rule.** `FROZEN_T` is -4 C and a named constant for the warm end of the ramp is
  -1 C. Bands walk on water, and drink from frozen sea, only below `FROZEN_T`. The globe
  draws the ice ramp between the two constants, which is what it draws today. The tooltip
  says "(frozen)" below `FROZEN_T` and "(thin ice)" between the two. The drawn ramp and the
  sim's threshold are the same two constants.
- **One daily swing.** The tooltip's current temperature peaks at 15:00, from the same
  definition the atmosphere model uses. The season interpolation has one CPU definition.
- **No constant defined by hand on both sides.** The shader gets the values of the
  duplicated constants above from the C++ definitions. (Suggestion: a `#define` block
  inserted after the `#version` line when the program is built.)
- **No stale or dead mirrors.** The `granaryNear` comment names `hutsNear`; `temperatureC`
  and `moistureAt` are gone from the shader.
- **Every mirror marked on both sides.** Each pair below carries a comment at both sites
  naming the other. A pair found beyond the table gets the same, and a line in the Run
  section.
- `Technical/Globe Viewer.md` states the lake and ice rules and that the CPU side is the
  source of truth for both.

The mirror pairs:

| CPU | Shader |
|---|---|
| `terrain::noise`, `fbm`, `ridged`, `blocks`, `warpedBlocks`, `continentField` | same names |
| `terrain::templateHeight` | `templateHeight` |
| `terrain::heightMeters` | `terrainHeight` |
| `terrain::Template::sample` | `earthAtTexel` (both sides already marked) |
| `plates::Field::sample` | `plateAtTexel`, `bsplineWeights` |
| `terrain::mixtureAt` | `substrateMix`, `coverMix` |
| `atmosphere::climFuzz` | `climFuzz` |
| `atmosphere::bilinearAt`, `seasonalAt`, `annualAt` | `climSample`, `climAnnual` |
| `atmosphere::derivedTempC`, `deriveAt` | `derivedTempC`, `derivedTCold`, `derivedTWarm`, `derivedMoist` |
| `atmosphere::seasonalTempC` | the temperature in `iceAt`, `snowCoverAt` |
| the CPU lake-shore rule (new) | `lakeLevelAt` |
| `sim::cellCentre` | `cellCentre` |
| `sim::granaryPos`, `farmsteadPos`, `villageRadiusKm`, `claimReach` | the blocks in `hutsNear` and `fieldsNear`, `villageRadiusKm`, `claimReach` |

## Files

A guide, not a limit:

- `shaders/globe.frag`
- `src/inspect.h`, `src/gl.h`, `src/main.cpp`
- `src/hydrology.h` (the lake rule), `src/bands.h` (the ice constants),
  `src/atmosphere.h` (the diurnal and season functions, mirror comments),
  `src/settlement.h` (`cachedSeasonT`)
- Comment-only: `src/terrain.h`, `src/plates.h`, `src/sphere.h`, `src/farmland.h`,
  `src/claims.h`, `src/textures.h`, `src/overlay.h`
- `Technical/Globe Viewer.md`

## Done when

1. `build.bat` and every `build_*.bat` exit 0 with no warning beyond the existing C4996
   set.
2. `grep -nE "^const (int|float) (HW|HH|NO_LAKE|PW|PH|HEIGHT_SCALE_M|CRUST_WEIGHT|LAND_RELIEF|RANGE_GAIN|NSUB|NCOV|SITE_STRIDE|HUT_KMPP|WALK_KMPP)\b" shaders/globe.frag`
   prints nothing, and neither does `grep -n "granaryNear\|float temperatureC\|float moistureAt" shaders/globe.frag src/*.h`.
3. The picture does not change. With `HH_BENCH=5`, `build\humanhistory.exe` screenshots at
   these views are byte-identical before and after the order (the shader's rules are
   unchanged; the constants it now receives have the values it had):
   `46.318 -174.111 4 7 30 60 0 0`, `46.318 -174.111 400 7 30 60 0 0`,
   `20 30 12000 7 30 60 0 0`, `46.5 10 4 earth 30 60 0 0.0012937`,
   `46.5 10 400 earth 30 60 0 0.0012937`.
4. The sim change is measured, not assumed. `build\test_resources.exe 7 40` and `3 40`
   complete; the Run section quotes their diff against the pre-order baseline, which may
   differ only where bands move or drink (band counts and positions, the settlements they
   found). `build\sweep.exe 7 0 rules 1` is byte-identical (the climate is untouched).
5. For every row of the mirror table, `grep` finds a comment naming the twin at both
   sites; the Run section lists the pairs and the lines.

## Depends on

Nothing. The CPU/GPU sampling probe that would machine-check the tooltip against the
picture is order 15, shaped separately; until it exists, the tooltip's agreement with the
picture is checked in the game in the morning.
