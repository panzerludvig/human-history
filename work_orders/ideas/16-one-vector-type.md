# 16 — One vector type for CPU geometry

**Status:** idea (2026-09-28) — split out of order 09; needs the design question below

## Problem

Three vector types do the same job: `camera::Vec3` (double), `terrain::V3` (float) and
`geodesic::D3` (double), with hand conversions between them, and three functions that turn
latitude and longitude into a direction.

Violates `standards/general.md` §Modules (a module hides one decision; three types for one
decision) and `standards/cpp.md` §Numbers, which already splits `float` for per-cell fields
from `double` for geometry and integration.

## Evidence

- `src/camera.h:17` `camera::Vec3`; `terrain::V3` (`src/terrain.h`); `geodesic::D3`
  (`src/geodesic.h`).
- Conversions at `inspect.h:61-62` (`describePoint`), `main.cpp:373`, `world.h:85`, the
  `camera::projectToScreen` calls in `overlay.h`, and `anchor.h` (2026-09-26).
- `camera::sphereDir` (`camera.h:38`), `atmosphere::unitAt` (`atmosphere.h:2941`) and
  `hydrology::cellDir` (`hydrology.h:46`) each map latitude and longitude to a unit vector.

Line references from order 09's evidence, checked against `f9c731f`; recheck before
planning.

## Design

_To be filled in._ The question: does the float `terrain::V3` stay as the type for noise
and per-cell fields beside one double geometry type (what `standards/cpp.md` §Numbers
suggests), or is there one type for everything?

## Outcome

_To be filled in._

## Files

_To be filled in._

## Done when

_To be filled in._ Probe outputs identical by seed, if the float/double split is kept.

## Depends on

Nothing.
