# Architecture

**Status:** Concept — see [[Meta/Status Vocabulary]]

## Tech Stack
- **Language:** C++17
- **Platform:** Windows (Win32 + OpenGL 3.3-level shaders on a compatibility context). No external libraries; the handful of GL functions needed are loaded by hand.
- **Build:** `build.bat` at the repo root — calls `vcvars64.bat` and `cl`, outputs `build\ironblood.exe` and copies `shaders\` next to it. Requires the VS 2022 "Desktop development with C++" workload.
- **Run:** `build\ironblood.exe [latDeg lonDeg [altitudeKm]]` — the optional start view exists for testing.

## Key Systems
- [[Technical/Globe Viewer]] — version 1: raycast sphere with procedural terrain; zoom and pan (Implemented)
- Event scheduler — priority queue of timed events with dependency-based invalidation ([[Design/Event-Driven]]) (not started)
- Spherical spatial index — positions, distances, and trajectory intersection on a sphere ([[Design/Spherical World]]) (not started)

## External Tools & Libraries
_None._

## Open Questions
- Terrain is currently a pure shader function with no CPU-side representation. The simulation will need to query height/biome at a point; either port the noise to C++ (same hash, same result) or move terrain to CPU-generated data and have the GPU sample it. Decide before any mechanic depends on terrain.
- Keep rolling our own windowing/GL loading, or adopt a small library once input and UI needs grow?
