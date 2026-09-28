# 15 — A probe that checks the shader against its CPU source

**Status:** idea (2026-09-28) — split out of order 06; needs its design settled (below)

## Problem

Every rule drawn by the shader has a CPU original, and the two are kept in step by hand and
by mirror comments. Nothing checks that they compute the same thing. Order 06 found three
drifted rules by reading the code; the next drift will be found the same way, or by a
player noticing that the tooltip and the picture disagree. `Technical/Architecture.md`
names this as the remaining risk.

Serves `standards/general.md` §Mirrored code ("a drift between them is a bug") and
§Verification (the probes are the test suite).

## Evidence

- Order 06, Evidence: lake shore, ice and diurnal rules drifted unnoticed between
  2026-08 and 2026-09-15.
- `Technical/Architecture.md` (the CPU/GPU sampling probe as the remaining risk).

## Design

_To be filled in._ The questions to settle before this can be planned:

- **What to compare.** Height, temperature and moisture are continuous and can be compared
  to a tolerance; cover and substrate mixtures too. Lake, river and ice are classes.
- **Tolerance.** Float differences between CPU and GPU transcendentals are expected; what
  size of difference is a drift.

How the probe reads the GPU side (a hidden GL context rendering to an offscreen target, or
the game's argv harness with a debug output) is the implementer's choice, not a design
question.

## Outcome

_To be filled in._

## Files

_To be filled in._ Likely `src/test_mirror.cpp`, `build_testmirror.bat`, a debug output in
`shaders/globe.frag`.

## Done when

_To be filled in._

## Depends on

Order 06, so the probe starts from rules that agree.
