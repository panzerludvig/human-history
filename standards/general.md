# General standards — Human History

Language-agnostic rules for this repo. Written as instructions for the developer and for
any coding agent; bullets are short and imperative. Language specifics: `cpp.md`. Agent
workflow: `agent-use.md`. Git conventions and the branch map: `Meta/Git.md`. Documentation
conventions: `Codex.md` and `Meta/Obsidian.md`.

These files are repo-local. They are written so that a rule which turns out to be
project-agnostic can move to a shared standards repo unchanged; only the import line in
`CLAUDE.md` changes when that happens.

## Dependencies

- None, by design. `Technical/Architecture.md` records this: Win32 and OpenGL, the handful
  of GL functions loaded by hand, nothing else.
- Adding a dependency is a decision recorded in `Technical/Architecture.md` under External
  Tools & Libraries, with the reason, before the code that uses it lands.

## Modules

- One header per concern, one namespace per header (`geodesic`, `hydrology`, `sim`, ...).
- One translation unit per executable: `main.cpp` and each probe include the headers they
  need and are compiled with a single `cl` call from their own `build_*.bat`.
- Every header opens with a prose comment saying why the module exists and which design or
  technical note it implements, by vault path (`Design/Migration.md`). `src/geodesic.h` is
  the model.
- A module hides one decision. If describing a header needs "and", split it.

## Names carry units

- A quantity's name ends in its unit: `prominenceM`, `coldestSeasonTempC`,
  `RIVER_MAJOR_KM2`, `workHours`, `travelHours`. A bare `height` or `temp` is a bug waiting
  for a caller to guess.
- Constants are `SCREAMING_CASE`, functions `camelCase`, types `PascalCase`, files
  `lowercase.h`. Accessors keep the established `fooAt(...)` (by position or cell) and
  `fooOf(...)` (by object) forms.
- No invented abbreviations. `clim`, `hy`, `ctx` are established; new ones are not.

## Mirrored code

- Anything that exists on both CPU and GPU — terrain and climate sampling in `src/terrain.h`
  and `shaders/globe.frag`, `climFuzz`, `derivedTempC` — is marked at both sites with a
  comment naming the other, and changes in both in the same commit.
- The CPU side is the source of truth; the shader is the copy. A drift between them is a
  bug, not a rendering choice.

## Thresholds and clocks

- A threshold derives from the table it gates. Write it as an expression over the table's
  constants, or assert at build time that the table can reach it. The settlement threshold
  above the maximum reachable K (Dev Log, 2026-08-23) is the recorded bug behind this rule.
- The clock never depends on there being something to clock. Time advances whether or not
  any settlement, band or event exists.

## Superseded models

- A model behind a flag is either still buildable on `main` or deleted from it. Dead code
  that no build compiles does not stay on the working branch.
- Reference-only code lives on a named branch listed in the branch map in `Meta/Git.md`,
  with a line saying what it holds. Code that is on no listed branch and in no vault note
  does not exist.

## Errors

- Fail loud. No silent NaN, no clamp that hides a value going out of range, no `catch` that
  swallows. A probe or the game aborting with a message beats a globe that quietly looks
  wrong.
- Assert invariants in new code: mixtures sum to one, conserved quantities stay conserved,
  indices are in range. The codebase has no asserts today; the rule binds new code and
  touched code, not a retrofit.

## Verification

- The probe programs (`src/test_*.cpp`, `sweep.cpp`, `transect.cpp`, `terrprobe.cpp`, ...)
  are the test suite. Each has its own `build_*.bat` and states its run line in its header
  comment.
- Runs are deterministic by seed. Same seed, same world, same numbers; a probe that cannot
  be reproduced from its seed is not evidence.
- After a change to climate, terrain or simulation, rebuild `build\humanhistory.exe` and
  verify in-game, not only the probe. The probes measure; the game is what the change is for.
- A behaviour change ships with the probe output that shows it, quoted in the commit body
  or the Dev Log entry.

## Documentation

- The Technical note for a system changes in the same commit as the code. It describes what
  is, in the present tense. The why goes to `Dev Log/Log.md`, as `Codex.md` says.
- Every path and identifier a note names exists at that commit.

## Deviations

- A deviation from these rules is written next to the code it applies to, with its reason.
  A silent deviation is a bug in the standards or in the code; either way it gets written
  down.
