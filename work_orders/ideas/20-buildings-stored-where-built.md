# 20 — Buildings: houses, granaries and farmsteads, stored where they are built

**Status:** idea (2026-09-28) — waits for 27 (the terrain deviation layer); takes over
farmsteads from order 13, which is to be dropped in the morning review of 2026-09-29

## Problem

Every village looks alike and none of its buildings has a history. The shader draws the
houses on a sunflower spiral round the village's 20 km cell centre, one per 12 people
(3 to 60), and the granaries on a golden-angle spiral of their own; neither looks at the
ground, and every village stands exactly on its cell's centre. Houses are not simulation
state: they appear and vanish as the population crosses multiples of 12. Farmsteads are a
count on a third spiral, and only the newest can be removed.

Order 13 was shaped to give farmsteads a chain of farms and ruins, placed by the ground,
with positions computed from the chain. On 2026-09-28 that was superseded: buildings of
every kind are **stored deviations** (order 27), placed once when built and kept where they
stand, tied to their settlement by owner rather than by position. A stored building
survives relocation, conquest and a change of culture with no special case, which a
computed position cannot.

## Evidence

- `shaders/globe.frag` `hutsNear` — the houses' spiral and count, computed per pixel; the
  granaries' spiral beside it; the village's bare ground as a disc round the cell centre.
- `src/farmland.h:25-40` — `hutCount`, `villageRadiusKm`, `granaryPos`.
- `work_orders/planned/13-farmsteads-placed-by-the-ground.md` — the farmstead chain, its
  placement by the ground, and its rules for abandonment, reoccupation and ruins.
- [[Design/Technology]] "Where farmsteads stand, and how they end" (designed 2026-09-28):
  the placement and the lifecycle this idea generalises; its positions-computed part is
  what is superseded.
- [[Design/Terrain]] and order 27: the deviation layer this builds on.

## Design

_Not shaped until order 27 is implemented._ What is decided, and the questions that stay
open until then:

- **Decided 2026-09-28**: one building model for everything a settlement builds (houses,
  granaries, farmsteads), each a stored deviation with a kind, an owner, a state (standing
  or ruin) and its dates; placed when built by a rule that reads the ground at the point
  (never water, not steep, flat preferred) and depends on the kind (houses close in,
  farmsteads out among their fields); never moved afterwards. Houses become simulation
  state. Farmsteads keep the lifecycle designed for order 13: a farm becomes a ruin when
  its fields are gone (spared until its first field, abandoned after 10 years without
  one), the farthest farm loses its fields first, ruins are reoccupied before new ground
  is built on, ruins weather over `RUIN_LIFE_DAYS`.
- **Open**: whether a village prefers riverbanks and shores and strings out along them;
  whether the village centre moves to the best ground near its cell centre; whether
  granaries stand with the houses or apart; how a shrinking village's houses become empty
  and then ruins.
- **Cultural styles** (a later wish): villages differing by culture in layout and looks,
  every culture starting with the same style and drifting apart. Needs culture drift,
  which [[Design/Culture]] still defers; likely its own order after this one.

## Outcome

_Not shaped until order 27 is implemented._

## Files

_To be filled in._

## Done when

_To be filled in._

## Depends on

27.
