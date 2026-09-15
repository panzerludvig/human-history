# 01 — Decide the fate of the physical rain path

**Status:** done (2026-09-15) — option 1; see `Dev Log/Log.md` 2026-09-15

## Problem

The physical water path in the atmosphere is unreachable from every build. `RULES`
defaults to `true` and nothing sets it `false`; the `continue` inside the cell loop exits
before the two-layer water model and the column rain, advection and diffusion. The sweep's
`phys` and `physgeo` modes therefore run painted rain while claiming to run physics, and
the two-layer water model receives zero evaporation because the buffers that feed it are
only filled after the `continue`.

This violates `standards/general.md` §Superseded models (a model behind a flag is buildable
or deleted; dead code does not stay on the working branch) and §Verification (a probe that
does not run what it says it runs is not evidence).

## Evidence

- `src/atmosphere.h:1028` — `inline bool RULES = true;`
- `src/sweep.cpp:282` — the only assignment, sets it `true`.
- `src/atmosphere.h:2702-2715` — the `if (RULES) { ...; continue; }`.
- `src/atmosphere.h:2716-2840` — WATER2 mirror and column rain, never reached.
- `src/atmosphere.h:2238-2304` — three moisture-transport passes whose fluxes are computed
  and never read.
- `src/atmosphere.h:2721` — `evapBuf/wUpBuf/wConvBuf` filled only here.

## Recommended change

This is a decision, not a mechanical refactor, and it gates order 02. Two options:

1. **Delete.** The prescribed climate is the game's climate since 2026-09-09
  (`Design/Weather.md`). Remove the column water path, the moisture-transport passes and
  the WATER2 mirror from `atmosphere.h`; the sweep modes that claimed to test them are
  renamed or removed. The physics stays on the reference branches listed in `Meta/Git.md`.
2. **Gate honestly.** Make `RULES` follow `PRESCRIBED` (or make the sweep's `phys` mode set
  it `false`), fill the WATER2 input buffers before the branch, and add a sweep probe
  output that shows physical rain differs from painted rain.

Recommendation: option 1, unless there is a near-term plan to bring the physics back on
the geodesic grid, in which case the geodesic port (`atmosphere_geo.h`, order 03) is the
place it comes back, not this file.

## Done when

- `grep RULES src/` returns either nothing (option 1) or a `false` assignment on a build
  path that a probe exercises (option 2).
- The Dev Log records which option and why.
- `build.bat` and `build_sweep.bat` succeed; the game's climate is unchanged by seed.

## Depends on

Nothing.
