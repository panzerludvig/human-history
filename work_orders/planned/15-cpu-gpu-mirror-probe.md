# 15 — A probe that checks the shader against its CPU source

**Status:** planned (2026-09-28)

Line references checked against 827c828 on 2026-09-28.

## Problem

Every rule drawn by the shader has a CPU original, and the two are kept in step by hand and
by mirror comments. Nothing checks that they compute the same thing. Order 06 found three
drifted rules by reading the code; the next drift will be found the same way, or by a
player noticing that the tooltip and the picture disagree.

Serves `standards/general.md` §Mirrored code ("a drift between them is a bug") and
§Verification (the probes are the test suite).

## Evidence

- Order 06, Evidence: the lake shore, ice and diurnal rules drifted unnoticed between
  2026-08 and 2026-09.
- `Technical/Architecture.md:40`: "The remaining risk is the two copies drifting apart; a
  test that compares CPU and GPU heights at sample points would close it."
- The mirror pairs are listed in order 06's Outcome (`work_orders/planned/06-cpu-gpu-drift.md`).

## Design

`standards/general.md` §Mirrored code and §Verification. Decided 2026-09-28:

- **What is compared:** terrain height; temperature and moisture at the point; the cover
  and substrate mixtures; and the classes (sea, lake, river, ice).
- **Tolerance:** measured, not guessed. The first run, on rules that agree after order 06,
  records how far CPU and GPU differ for each quantity; the tolerance is set at a few times
  that, and written next to the probe with the run it came from. Classes may disagree only
  at a boundary: where the CPU's own value lies within the tolerance of the threshold that
  decides the class.
- **Where it looks:** a few hundred random points plus points deliberately placed at
  boundaries (shorelines, lake shores, the ice and snow lines, the edges of mountain
  ranges), where drift shows first.

How the probe reads the GPU side (a hidden GL context rendering to an offscreen target, or
the game's argv harness with a debug output) is the implementer's choice.

## Outcome

- A probe, with its own `build_*.bat` and its run line in its header comment, that compares
  the CPU and GPU values of the quantities above at the sample points of a world and fails
  loud, naming the quantity, the point and both values, when a difference is beyond
  tolerance or a class disagrees away from a boundary.
- The tolerances, with the measurement they came from, are written next to the probe.
- `Technical/Architecture.md` says the risk it named is now covered and how to run the
  check; `standards/general.md` §Mirrored code names the probe as the check a change to a
  mirrored rule runs.

## Files

A guide, not a limit: a new probe source and `build_*.bat`, a debug output in
`shaders/globe.frag` if the implementer reads the GPU through the shader,
`Technical/Architecture.md`, `standards/general.md`.

## Done when

1. `build.bat` and every `build_*.bat` exit 0 with no warning beyond the existing C4996 set.
2. The probe passes on seed 7 and on the Earth template, and prints for each quantity the
   number of points, the largest difference and the tolerance, and for the classes the
   number of disagreements and how many were at a boundary. The Run section quotes it.
3. **It catches a drift.** In a scratch copy of the shader, changing one mirrored rule
   (the lapse rate from 6.5 to 7.0) makes the probe fail on the temperature comparison,
   and restoring it makes the probe pass. The Run section quotes both runs. The change is
   not committed.
4. Game screenshots at the five views of order 06's Done when are byte-identical before and
   after: a debug output, if added, changes nothing in normal drawing.

## Depends on

06: the probe starts from rules that agree, and its tolerances are measured there.
