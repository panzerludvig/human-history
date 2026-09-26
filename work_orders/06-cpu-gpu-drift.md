# 06 — Close the CPU/GPU drifts and mark every mirror

**Status:** open (2026-09-26) — 05 is merged and the tooltip and shader load have left
`main.cpp`, so nothing blocks it; it shares `shaders/globe.frag` with order 13 and
`src/inspect.h` with orders 11 and 13, so not the same night as either

Line references re-checked against f9c731f on 2026-09-26; those into `shaders/globe.frag` moved to 5087977.

## Problem

Two mirrored rules have drifted, one mirror comment names a function that no longer
exists, most mirror comments are one-sided (the shader names the CPU, the CPU rarely names
the shader), two mirrored pairs carry no comment on either side, and two shader functions
are dead mirrors with no callers.

Violates `standards/general.md` §Mirrored code.

## Evidence

- **Lake level drift.** `src/inspect.h:194-211` (`inspect::describePoint`, the tooltip):
  max lake level over a 3x3, shore at `h < lake + 12`, under a comment at `inspect.h:108`
  that still calls it "same rule as the shader". `shaders/globe.frag:364` `lakeLevelAt`:
  smoothstep-weighted 2x2 average, no +12. The tooltip and the picture disagree about where
  a shore is.
- **Ice drift.** `inspect.h:102, 206` (`describePoint`) use `seasonalT < FROZEN_T` (hard,
  -2 C; `FROZEN_T` is now `src/bands.h:20`); `globe.frag:832` `iceAt` is
  `smoothstep(-1, -4)`. In the -1..-4 C band the tooltip says frozen while the globe draws
  a ramp.
- **Stale name.** `src/farmland.h:16` (the comment over `sim::granaryPos`) cites
  `granaryNear` in the shader; it is now `hutsNear` (`globe.frag:476-566`).
- **Diurnal peak.** `inspect.h:79` (`describePoint`) peaks at 14:00, `atmosphere.h:925`
  (`Prescribed::surfaceT`) at 15:00.
- **No comment either side:** `atmosphere::bilinearAt/annualAt/seasonalAt`
  (`atmosphere.h:2978-3001`) vs `climSample/climAnnual` (`globe.frag:753-778`), where the
  CPU clamps rows to `[0, H-1]` (`atmosphere.h:2982`) and the shader clamps to
  `[0.02, 0.98]`; `sim::cellCentre/cellOf` (`src/sphere.h:12-17`) vs `globe.frag:381`
  `cellCentre`.
- **One-sided comments:** `terrain.h:59-176` noise stack, `templateHeight/heightMeters`
  (252, 270), `derivedTempC` and kin (`atmosphere.h:3016-3123`, `derivedTempC` through
  `deriveAt`).
- **Dead mirrors:** `globe.frag:905` `temperatureC`, `:910` `moistureAt` have no callers in
  the shader.
- **Constants duplicated with no check:** `HW/HH` (`globe.frag:60` vs `hydrology.h:18`),
  `SITE_STRIDE` (`src/textures.h:84` vs `globe.frag:392`, above `siteTexel`),
  `HUT_KMPP/WALK_KMPP` (`src/overlay.h:85-86` vs `globe.frag:464-465`, above `hutsNear`),
  `NSUB/NCOV` (`terrain.h:355-356` vs `globe.frag:921-922`), `NO_LAKE` (`hydrology.h:19` vs
  `globe.frag:61`), `CRUST_WEIGHT` (`terrain.h:20` vs `globe.frag:53`).
- The 4-season interpolation `fmod(t,365)/365*4-0.5` is written out at `inspect.h:88`
  (`describePoint`), `atmosphere.h:2997` (`seasonalAt`), `settlement.h:873`
  (`population::cachedSeasonT`) and `globe.frag:758` (`climSample`).
- Line references checked against commit `9f598d3` on 2026-09-15, after orders 01 and 02
  landed; re-checked against `f9c731f` on 2026-09-26, after 03, 04, 05 and 08 moved the
  tooltip to `inspect.h` and split `sim.h`. Every drift above still holds.

## Design

`standards/general.md` §Mirrored code. `Technical/Globe Viewer.md` records which side is
the source of truth for each mirror and any rule the picture deliberately softens;
`Technical/Architecture.md` names the CPU/GPU sampling probe as the remaining risk.

## Recommended change

1. Pick the CPU rule for lake shore and ice (the standard says the CPU is the source of
  truth) and make the shader match, or write down in `Technical/Globe Viewer.md` why the
  picture is deliberately softer than the tooltip. Settle 14:00 vs 15:00 the same way.
2. Fix the `granaryNear` comment; delete the two dead shader mirrors.
3. Add a "mirrors shaders/globe.frag <name>" comment on every CPU site in the table above
  and the reverse on every shader site lacking one.
4. Generate the duplicated constants: a tiny script or a `#define` block emitted into the
  shader source at load time from the C++ constants, so `HW`, `SITE_STRIDE` and the rest
  have one definition.
5. A probe that samples height, temperature and moisture at a few hundred points on both
  CPU and a GL offscreen render would close the class of bug; `Technical/Architecture.md`
  already names it as the remaining risk. Scope it here or as its own order.

## Files

- `shaders/globe.frag`
- `src/inspect.h` (`describePoint`, the tooltip that was in `main.cpp`) and `src/gl.h`
  (`gl::buildProgram`, the shader load that was in `main.cpp`); `main.cpp` itself is no
  longer touched
- `src/atmosphere.h`, `src/terrain.h`, `src/hydrology.h`, `src/sphere.h`,
  `src/farmland.h`, `src/textures.h`, `src/overlay.h` (mirror comments and the duplicated
  constants; comment-only changes outside `atmosphere.h`)
- New, if step 5 is scoped here: `src/test_mirror.cpp`, `build_testmirror.bat`
- `Technical/Globe Viewer.md`, `Technical/Architecture.md`

## Done when

- The tooltip and the drawn globe agree on shore and ice for a fixed seed and view.
- Every function in the mirror table carries a comment naming its twin on both sides.
- No constant is defined in both C++ and GLSL by hand.

## Depends on

Nothing.
