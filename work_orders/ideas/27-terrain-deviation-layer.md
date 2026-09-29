# 27 — The terrain deviation layer

**Status:** idea (2026-09-28) — the design is open

## Problem

[[Design/Terrain]] (status Concept) says the terrain is a stack of functions and that only
deviations from those functions are stored: a clearing, a field, a planted wood, each a
record with a place, an owner and a time, whose state follows a known curve from its start
(a clearing becomes scrub, then wood). That layer was never built. What exists instead is
three special cases, each shaped for one use:

- **Fields**: a tilled area per farmstead slot on each settlement, drawn by the shader
  from settlement data and painted over the ground colour. The cover underneath does not
  change: a field in a forest is forest to everything but the drawing.
- **Scars**: the land condition left where a settlement stood, one per 20 km cell.
- **Ruins**: a list of abandoned settlements, one point each, marked in the pop texture.

Nothing changes what grows at a point, nothing regrows, and nothing can be placed at a
point finer than the 20 km grid except through its settlement. Buildings (order 20) need
exactly what the design describes: records at points, with an owner and a time, that
outlive whoever made them and are drawn where they stand.

## Evidence

- [[Design/Terrain]] — the concept: "only deviations from those functions are ever stored";
  "cutting a forest stores one deviation, not a repaint of the world"; "regrowth is an
  event"; "deviations are map objects with a place, an owner and a time".
- `src/settlement.h:660` `Settlement::tilled[]`; `shaders/globe.frag` `fieldsNear` (the
  fields drawn from settlement data).
- `src/settlement.h:724-729` `Field::scars`, an `unordered_map` by 20 km cell.
- `src/settlement.h:731-736` `Field::Ruin`, `Field::ruins`; `src/textures.h:51` marks a
  ruin's cell; `globe.frag` `ruinNear` draws it.
- [[Design/Resources]] ("Farming clearance (the terrain-deviation work)", deferred) and
  [[Design/Technology]] (cleared fields as terrain deviations, decided 2026-09-01).
- The discussion of 2026-09-28: buildings are stored deviations rather than positions
  computed from their settlement, because a stored building survives relocation, conquest
  and a change of culture with no special case.

## Design

_To be filled in._ The questions:

- **The record**: position (at what precision; the anchor work showed a float unit vector
  is only good to 40 cm), kind, owner, the time it began and the times of its state
  changes, and what each kind adds (a building's kind and style, a clearing's extent).
- **Kinds**: buildings, fields, clearings, ruins, scars; which come first, and whether the
  existing three special cases move onto the layer in the same order or later.
- **State over time**: each kind's curve (a ruin weathers, a clearing regrows, a field
  reverts when untended as order 12 has it), and the events that end or change it
  ([[Design/Event-Driven]]).
- **What reads it**: the drawing (the shader finds the deviations near each pixel), the
  simulation (what grows at a point, for yields and wood), the tooltip. How the
  simulation's 20 km cells see deviations smaller than a cell.
- **Scale**: tens of thousands of records today, possibly millions later. A record of
  roughly 24-32 bytes makes a million about 24-32 MB, in memory and in the save; the cost
  that matters is the spatial index the shader reads and keeping it current as records
  change.
- **Saves**: the layer is state, so it is saved; how old saves come in.

## Outcome

_To be filled in._

## Files

_To be filled in._

## Done when

_To be filled in._

## Depends on

Nothing.
