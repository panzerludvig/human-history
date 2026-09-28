# 20 — Village buildings placed by the ground, like farmsteads

**Status:** idea (2026-09-28) — the design questions below are open

## Problem

Every village is laid out by the same formula, so every village looks alike at close zoom,
and the layout ignores the ground it stands on. Houses are a sunflower spiral around the
village's 20 km cell centre, jittered by a hash; granaries are a golden-angle spiral of
their own. Neither looks at water, slope or the lie of the land, and the village itself is
always centred on its cell's centre point.

Order 13 replaces the same kind of spiral for farmsteads with a placement that reads the
ground, is seeded per settlement, never moves in play, and records a chain of farms and
ruins so any one can be removed. The idea is to give the village's own buildings the same
treatment.

## Evidence

- `shaders/globe.frag:476-523` (`hutsNear`) — the houses: count `clamp(P / 12, 3, 60)`,
  placed on a sunflower spiral from the cell centre with a hashed jitter and facing, computed
  per pixel in the shader and nowhere else; the village's bare ground is a disc of
  `villageRadiusKm * 1.2` round the cell centre.
- `src/farmland.h:25-29` — `hutCount`, `villageRadiusKm`, the CPU side of the house count
  and the village's size.
- `src/farmland.h:32-40` — `granaryPos(cell, k)`, a golden-angle spiral, mirrored in the
  shader's granary loop (`globe.frag:512-518`).
- Houses are not simulation state: they follow the population, appearing and vanishing as
  `P` crosses multiples of 12.
- [[Design/Technology]], "Where farmsteads stand, and how they end" (designed 2026-09-28,
  order 13): the pattern this idea would reuse.

## Design

_To be filled in._ The questions to settle before this can be planned:

- **Are houses state?** Today they are derived from the population. Farmsteads become a
  chain of farms and ruins; should houses too, so a shrinking village leaves empty houses
  and ruins inside it rather than houses silently disappearing, or should houses stay
  derived from the population, with only their placement changing?
- **What the ground decides.** Never water and not steep, as for farmsteads; does a village
  also prefer a riverbank or a shore, and does it cluster along it rather than round a
  point?
- **Where the village centre is.** Always the 20 km cell's centre today. Should the village
  stand at the best ground near that point (a riverbank, a rise), found the same way?
- **Granaries**: placed with the houses by the same rule, or apart, as stores often were?
- **Cultural styles** (added 2026-09-28). Villages could differ by culture in how they are
  laid out (clustered or strung out, round a centre or along a line, how tightly packed,
  which way houses face) and in how buildings look (shape, size, roof colour). The wish:
  every culture starts with the same style, and styles drift apart over time, so a world's
  early villages look alike and its later ones show who built them. Questions:
  - Which traits make up a style, and which of them the ground can override (a strung-out
    culture on a hilltop still clusters).
  - How a style drifts: slowly and at random per culture, pulled by what the culture lives
    on (the affinities in [[Design/Culture]] already drift toward hunting, farming,
    herding), or both; and whether a colony inherits its parent's style and then drifts
    on its own.
  - Whether a village rebuilt by another culture (conquest, resettlement) keeps the old
    houses in the old style until they are replaced. That is only possible if houses are
    state (the first question).
  - A new world starts with about one culture per settlement (400 cultures among 401
    settlements in the seed 7 test run), so "the same at the start" means every culture
    starts with the same style. It does not mean a single culture that later splits.

## Outcome

_To be filled in._

## Files

_To be filled in._ Likely `src/farmland.h`, `src/textures.h`, `shaders/globe.frag`
(`hutsNear`), `src/inspect.h`, and `src/settlement.h` and `src/savefile.h` if houses become
state.

## Done when

_To be filled in._

## Depends on

13: it reuses the placement and the chain that order 13 builds for farmsteads.

The cultural styles also need culture drift, which [[Design/Culture]] lists as deferred
("culture drift and new cultures forming", "culture-level traits"). Either that is designed
first, or this order is split: placement by the ground now, styles when cultures can drift.
