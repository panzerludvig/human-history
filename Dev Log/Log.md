# Dev Log

## 2026-08-22 — Project Setup

### What was done
Created the Iron and Blood vault by salvaging the workflow and structure of Delegate, pushed to https://github.com/panzerludvig/iron-and-blood.

### Decisions and reasoning

**Inherit Delegate's workflow, not its design**
Delegate's process — Codex, Meta notes, Dev Log as a reasoning archive, explicit session boundaries — worked and was kept almost verbatim. Its design content was *not* copied: Delegate accumulated many interdependent concepts before any was tested, and the open-question lists grew faster than they could be resolved. The concepts are listed as candidates in [[Design/Core Concepts]] so they are available when needed, but none is decided. Full accounting in [[Meta/Inherited from Delegate]].

**Incremental Design as a Codex principle**
The one new rule: start with the minimum for a playable loop and expand outward. Written as [[Meta/Incremental Design]] and enforced via a "scope check" recurring task and a status on every design note ([[Meta/Status Vocabulary]]).

**Status vocabulary defined now**
Delegate left this as a todo. Defining it before any design note exists means every note starts with an honest status rather than retrofitting later.

**Git conventions filled in**
Kept minimal — direct to `main`, imperative subjects — since the repository is notes-only. Branching deferred until there is code.

---

## 2026-08-22 — First Core Concepts

### What was done
Promoted three concepts to Core: [[Design/Map-Centric]], [[Design/Event-Driven]], [[Design/Spherical World]]. Each got a fresh note written from scratch rather than copied from Delegate.

### Decisions and reasoning

**Foundational concepts decided before the first increment**
The promotion procedure in [[Design/Core Concepts]] says a concept enters Core only if a desired end state needs it. These three were admitted without that step because they are constraints on the *shape* of every mechanic, not mechanics themselves — choosing them later would mean rewriting whatever came first. The procedure still applies to everything else.

**Event-driven stated as a prohibition on polling, with two sanctioned alternatives**
Compute-the-moment (schedule an event at a calculated time, invalidate on state change) is preferred; trigger-on-manipulation (check a condition only when a dependency is touched) is the fallback. Collision is the worked example. Writing it as "never poll" rather than "prefer events" makes violations obvious.

**Spherical and Earth-like**
Earth-like is a default for scale and intuition, not a requirement to model Earth. Coordinate representation deliberately left open.

---

## 2026-08-22 — Version 1: Globe Viewer

### What was done
Built and ran the first executable: a Win32/OpenGL window showing a procedural Earth-like globe with wheel zoom (10 km across the screen up to one full side of the globe) and left-drag pan. Documented in [[Technical/Globe Viewer]]; stack recorded in [[Technical/Architecture]].

### Decisions and reasoning

**C++ with no dependencies**
The machine had MSVC but no CMake or package manager, and the VS C++ workload had to be installed during the session. Raw Win32 + hand-loaded OpenGL avoids a dependency decision before there is anything to depend on. Revisit when input/UI needs grow.

**Raycast sphere, procedural terrain in the fragment shader**
A mesh-and-texture globe needs a tiling/LOD system to reach 10 km; a raycast sphere with a noise function of the surface normal reaches any zoom with one code path. The cost is that terrain has no CPU representation yet — flagged as an open question rather than solved now, per [[Meta/Incremental Design]].

**Integer hash for noise**
A sin-based hash visibly breaks at the coordinate magnitudes reached at max zoom. pcg3d on integer lattice coordinates is stable everywhere.

**Sun fixed to the camera**
A world-fixed sun leaves half the globe dark at the zoomed-out view, which is useless for a map. Lighting is for legibility, not realism.

**Tuning done by screenshot**
Land fraction, snow extent, and relief visibility were each judged from captured views at several positions and zooms rather than guessed. The starting bias put almost a whole hemisphere underwater; snow originally covered mid-latitude lowlands; relief was invisible until the sun was moved off the camera axis.
