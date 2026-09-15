# 06 — Close the CPU/GPU drifts and mark every mirror

**Status:** open (2026-09-15)

## Problem

Two mirrored rules have drifted, one mirror comment names a function that no longer
exists, most mirror comments are one-sided (the shader names the CPU, the CPU rarely names
the shader), two mirrored pairs carry no comment on either side, and two shader functions
are dead mirrors with no callers.

Violates `standards/general.md` §Mirrored code.

## Evidence

- **Lake level drift.** `src/main.cpp:2039-2140` (tooltip): max lake level over a 3x3,
  shore at `h < lake + 12`. `shaders/globe.frag:274` `lakeLevelAt`: smoothstep-weighted
  2x2 average, no +12. The tooltip and the picture disagree about where a shore is.
- **Ice drift.** `main.cpp:2034, 2136` use `seasonalT < FROZEN_T` (hard, -2 C);
  `globe.frag:745` `iceAt` is `smoothstep(-1, -4)`. In the -1..-4 C band the tooltip says
  frozen while the globe draws a ramp.
- **Stale name.** `src/sim.h:566` cites `granaryNear` in the shader; it is now `hutsNear`
  (`globe.frag:422-450`).
- **Diurnal peak.** `main.cpp:2007` peaks at 14:00, `atmosphere.h:967` at 15:00.
- **No comment either side:** `atmosphere::bilinearAt/seasonalAt/annualAt`
  (`atmosphere.h:3248-3269`) vs `climSample/climAnnual` (`globe.frag:660-676`), where the
  CPU clamps rows to `[0, H-1]` and the shader clamps to `[0.02, 0.98]`;
  `sim::cellCentre/cellOf` (`sim.h:38-43`) vs `globe.frag:291`.
- **One-sided comments:** `terrain.h:59-176` noise stack, `templateHeight/heightMeters`
  (252, 270), `derivedTempC` and kin (`atmosphere.h:3289-3367`).
- **Dead mirrors:** `globe.frag:818 temperatureC`, `:823 moistureAt` have no callers in
  the shader.
- **Constants duplicated with no check:** `HW/HH` (`globe.frag:49` vs `hydrology.h:18`),
  `SITE_STRIDE` (`main.cpp:897` vs `globe.frag:302`), `HUT_KMPP/WALK_KMPP`
  (`main.cpp:944` vs `globe.frag:375`), `NSUB/NCOV`, `NO_LAKE`, `CRUST_WEIGHT`.
- The 4-season interpolation `fmod(t,365)/365*4-0.5` is written out at `main.cpp:2020`,
  `atmosphere.h:3270`, `globe.frag:666`.

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
- `src/main.cpp` (tooltip block and shader load only; overlaps order 05, so not the same
  night)
- `src/atmosphere.h`, `src/terrain.h`, `src/hydrology.h`, `src/sim.h` (mirror comments and
  the duplicated constants; comment-only changes outside `atmosphere.h`)
- New, if step 5 is scoped here: `src/test_mirror.cpp`, `build_testmirror.bat`
- `Technical/Globe Viewer.md`, `Technical/Architecture.md`

## Done when

- The tooltip and the drawn globe agree on shore and ice for a fixed seed and view.
- Every function in the mirror table carries a comment naming its twin on both sides.
- No constant is defined in both C++ and GLSL by hand.

## Depends on

Nothing.
