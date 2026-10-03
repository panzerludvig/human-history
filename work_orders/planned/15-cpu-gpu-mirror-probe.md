# 15 — A probe that checks the shader against its CPU source

**Status:** planned (2026-10-03): failed on the night of 2026-10-02 and reworked: where the probe finds the shader drifted from the CPU, the shader follows the CPU, and the picture may change by those fixes

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
- `Technical/Architecture.md`, on the terrain mirrored on CPU and GPU: "The remaining
  risk is the two copies drifting apart; a test that compares CPU and GPU heights at
  sample points would close it."
- The mirror pairs are listed in order 06's Outcome
  (`work_orders/implemented/06-cpu-gpu-drift.md`), each marked at both sites since.
- The night of 2026-10-02 built the probe and it found two drifts left after 06 (Run
  section below): the shader's swamp rule is older than the CPU's
  (`atmosphere::swampFromBalance`), so the globe draws more marsh than the simulation
  has; and the shader stops its climate lookup at 86.4 degrees where the CPU reads on to
  the pole, up to 1.9 C apart on Earth.

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

Decided 2026-10-03: **where the probe finds the shader drifted from the CPU, the shader
follows the CPU** (`standards/general.md`: the CPU is the source of truth, a drift is a
bug). That includes the swamp rule and the polar climate lookup found on 2026-10-02, and
any further drift the probe finds. The picture may change by those fixes; the probe's
own readout must change nothing.

## Outcome

- A probe, with its own `build_*.bat` and its run line in its header comment, that compares
  the CPU and GPU values of the quantities above at the sample points of a world and fails
  loud, naming the quantity, the point and both values, when a difference is beyond
  tolerance or a class disagrees away from a boundary.
- The tolerances, with the measurement they came from, are written next to the probe.
- Every drift the probe finds is fixed on the shader's side, so the probe passes on
  `main`.
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
4. The probe's readout changes nothing in normal drawing: with the readout added and no
   rule changed, game screenshots (with `HH_BENCH=5`) at the five views of order 06's Done
   when are byte-identical to `main`'s.
5. The drift fixes are quoted, for the morning: for each fix, what changed on the shader's
   side, and per view the share of pixels changed and the largest change.

## Depends on

06 and 09 (both implemented): the probe starts from the mirrored rules and the shared
constants as they are on `main`.

## Run

**Outcome:** failed. Branch `wo/15-cpu-gpu-mirror-probe`, commits `7e704b8` (the probe)
and `f5029ab` (the shader follows two CPU rules it had drifted from). Not merged: Done
when 2 and 4 cannot hold at the same commit, and the branch also conflicts with 09 on
`nightly` in a way that needs lines neither order wrote.

What it did: `build_testmirror.bat` and a probe that opens a hidden GL context, draws one
row of an off-screen RGBA32F target per sample point under the anchor uniforms, and
compares the shader's readout with the CPU's at 600 points per world (300 random, and
points on shorelines, lake shores, river banks, the ice and snow lines and mountain
edges). The shader's readout is compiled only under `#ifdef MIRROR_PROBE`. Tolerances are
4x the largest difference measured once the rules agree, on an Intel Arc 140V.

The two drifts the probe found, at `7e704b8`:

- **Swamp.** The CPU's `atmosphere::swampFromBalance` is `0.45 * smoothstep(1.5, 4.0,
  balance)`; the shader still has `0.55 * smoothstep(0.5, 2.5)`, so the globe draws more
  mud and marsh than the simulation has (seed 7: swamp off by up to 0.43, 170 failures).
- **The poles.** The shader clamps its climate lookup to 0.02-0.98 of the band, so beyond
  86.4 degrees it reads 86.4; the CPU reads on to the last row (earth: up to 1.63 C in
  the annual temperature, 1.92 C in the warmest season).

Done when, at `f5029ab`, where the shader follows the CPU on both:

1. All builds exit 0, C4996 only, `build_testmirror.bat` included.
2. PASS on seed 7 and on earth, 600 points each. Largest difference seed 7 / earth
   against the tolerance: height 0.176 / 0.389 m (1.6); temperature 0.0115 / 0.0134 C
   (0.054); moisture 0.00046 / 0.00131 (0.0053); coldest season 0.0312 / 0.0396 (0.16);
   warmest 0.0103 / 0.0091 (0.042); season 0.0098 / 0.0241 (0.097); swamp 0.00030 /
   0.00016 (0.0013); lake level 0.00024 / 0.00006 m (0.00098); river margin 0.067 /
   0.068 km (0.28); substrate 0.0020 / 0.0029 (0.012); cover 0.0034 / 0.0032 (0.014).
   Classes, disagreements / at a boundary, seed 7 then earth: sea 47/47, 46/46; lake
   20/20, 18/18; river 15/15, 13/13; ice 39/39, 46/46.
3. Lapse 6.5 -> 7.0 in a scratch copy of the shader: FAIL, temperature on 155 points
   (seed 7, largest 0.787) and 210 (earth, largest 1.06); restored, both pass. Not
   committed.
4. **Fails.** At `7e704b8` all five screenshots are byte-identical to `main`: the probe's
   readout changes nothing. At `f5029ab` four of the five change, by the two drift fixes
   (`46.318 -174.111 400 7`: 110,344 pixels, at most 4 levels; `20 30 12000 7`: 3,522
   pixels; `46.5 10 400 earth`: 76,212 pixels, 1 level; `46.5 10 4 earth`: 1 pixel).

Merge into `nightly`: conflicts in `shaders/globe.frag` (15 replaced the climate lookups
with `climUV` and `seasonalTempAt`, which 09 rewrote around `DAYS_PER_YEAR`,
`LAPSE_K_PER_KM` and `EARTH_RADIUS_KM`), `src/main.cpp` (15 moved `globeConstants` to a
new `src/globeconstants.h`; 09 added five constants to it) and both Technical notes.
Fitting them needs lines neither order wrote, so it would have been deferred had it
passed.

Files beyond the list: `src/globeconstants.h` (new), `src/hydrology.h` (a CPU original
for the river rule, `riverMarginKm`), `src/textures.h`, `src/gl.h`, `src/main.cpp`,
`Technical/Globe Viewer.md`; a line in `standards/general.md` saying a change to a
mirrored rule runs the probe.

Unsure of: whether the shader should follow the CPU on the swamp and the poles (the
standard says the CPU is the source of truth, but the order held the picture fixed). The
tolerances were measured on one GPU. The river margin's tolerance (0.28 km) is close to a
river's 0.3 km half-width, so the river class is checked loosely. The ice ramp's
in-between values and snow cover have no CPU twin and are not compared.

## Review

**Rework**, 2026-10-03. The probe works and found two real drifts. The developer's decision: the CPU is the source of truth, so the shader follows it on the swamp and the poles, and Done when 4 now holds only the probe's own readout to changing nothing. Back to `planned/`, to be implemented again from `main`, which now has 09's constants. The night's branch `wo/15-cpu-gpu-mirror-probe` (`7e704b8`, `f5029ab`) is kept until then, as a starting point the implementer may reuse.

Review note for the night: [[Dev Log/Nightly/2026-10-02]].
