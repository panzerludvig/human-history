# Architecture

**Status:** Concept — see [[Meta/Status Vocabulary]]

## Tech Stack
- **Language:** C++17
- **Platform:** Windows (Win32 + OpenGL 3.3-level shaders on a compatibility context). No external libraries; the handful of GL functions needed are loaded by hand.
- **Build:** `build.bat` at the repo root — calls `vcvars64.bat` and `cl`, outputs `build\ironblood.exe` and copies `shaders\` next to it. Requires the VS 2022 "Desktop development with C++" workload.
- **Run:** `build\ironblood.exe [latDeg lonDeg [altitudeKm]]` — the optional start view exists for testing.

## Key Systems
- [[Technical/Globe Viewer]] — raycast sphere with procedural terrain in metres; zoom and pan; worlds; hydrology layer (Implemented)
- Event scheduler — priority queue of timed events with dependency-based invalidation ([[Design/Event-Driven]]) (not started)
- Spherical spatial index — positions, distances, and trajectory intersection on a sphere ([[Design/Spherical World]]) (not started)

## External Tools & Libraries
_None._

## Open Questions
- Terrain is a function mirrored on CPU and GPU (`src/terrain.h` ↔ `shaders/globe.frag`). The pattern now in use: the function is the source of truth, derived layers (hydrology) are computed on the CPU and handed to the GPU as textures. Terrain modification will be the next derived layer — see [[Meta/Open Threads]]. The remaining risk is the two copies drifting apart; a test that compares CPU and GPU heights at sample points would close it.
- Keep rolling our own windowing/GL loading, or adopt a small library once input and UI needs grow?
