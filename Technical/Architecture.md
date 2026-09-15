# Architecture

**Status:** Concept — see [[Meta/Status Vocabulary]]

## Tech Stack
- **Language:** C++17
- **Platform:** Windows (Win32 + OpenGL 3.3-level shaders on a compatibility context). No external libraries; the handful of GL functions needed are loaded by hand.
- **Build:** `build.bat` at the repo root — calls `vcvars64.bat` and `cl`, outputs `build\humanhistory.exe` and copies `shaders\` next to it. Requires the VS 2022 "Desktop development with C++" workload.
- **Run:** `build\humanhistory.exe [latDeg lonDeg [altitudeKm [seed [land% [concentration% [plates]]]]]]` — the optional start view exists for testing; `plates` enables the plate debug overlay.

## Key Systems
- [[Technical/Globe Viewer]] — raycast sphere with procedural terrain in metres; zoom and pan; worlds; plate and hydrology layers (Implemented)
- Event scheduler — settlements schedule their own re-evaluations (population.h); a general queue with dependency invalidation is still to come ([[Design/Event-Driven]]) (begun)
- Spherical spatial index — positions, distances, and trajectory intersection on a sphere ([[Design/Spherical World]]) (not started)

## Modules

One header per concern under `src/`, each opening with the note it implements; every executable is one translation unit that includes what it needs (`standards/general.md` §Modules).

- `src/main.cpp` — the window, the render loop and the wiring between the modules below.
- `src/gl.h` — the OpenGL entry points loaded by hand and the shader program built from `shaders\`.
- `src/camera.h` — the camera above the globe, its vector algebra, and the zoom and drag rules that move it.
- `src/world.h` — a world: seed and menu parameters, and `World::build`, which derives everything from them in dependency order and reports each stage through a progress callback.
- `src/savefile.h` — the save file: what the seed cannot regenerate, as keyed text lines; `src/test_savefile.cpp` (`build_testsavefile.bat`) round-trips a world through it.
- `src/inspect.h` — what the viewer says about a place or a people, as text: the tooltip line and the panel tabs, all over a `const World&`.
- `src/terrain.h`, `src/plates.h`, `src/hydrology.h`, `src/atmosphere.h` (with `atmosphere_geo.h`, `dynamics2.h`, `qg2.h`, `qg2geo.h`, `water2geo.h`), `src/daylight.h`, `src/population.h`, `src/technology.h`, `src/sim.h` — the world and its simulation, described in [[Technical/Globe Viewer]] and the design notes their headers name.

## External Tools & Libraries
_None._

## Open Questions
- Terrain is a function mirrored on CPU and GPU (`src/terrain.h` ↔ `shaders/globe.frag`). The pattern now in use: the function is the source of truth; layers are computed on the CPU and handed to the GPU as textures, either feeding the function (plates) or derived from it (hydrology). Generation order: seed → plates → sea level → hydrology. Terrain modification will be the next derived layer — see [[Meta/Open Threads]]. The remaining risk is the two copies drifting apart; a test that compares CPU and GPU heights at sample points would close it.
- Keep rolling our own windowing/GL loading, or adopt a small library once input and UI needs grow?
