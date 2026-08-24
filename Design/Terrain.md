# Terrain

**Status:** Concept — see [[Meta/Status Vocabulary]]

Core concept: terrain is a stack of layers, each a function of the layers below it, and only deviations from those functions are ever stored.

---

## Layers

| Layer | What it is | Changes | Stored |
|-------|-----------|---------|--------|
| Height | Metres above sea level — plates, continents, mountains | Never (until the overlay exists) | Nothing; seed |
| Substrate | What the ground is made of — a mixture of soil, sand, rock, scree, silt, mud, ice | Geologically; never in play | Nothing; function of height, slope, climate, rivers |
| Climate | Temperature and moisture | Slowly, not by the player | Nothing; function of latitude, altitude, noise (rain shadow later) |
| Cover | What grows — a mixture of bare ground, tundra, taiga, forest, rainforest, grassland, steppe, savanna, shrub, marsh, desert | By the player and by time | Deviations only |

Nothing at a point is a single type. Every point is a **mixture**: substrate fractions and cover fractions, each summing to 1 — "70% forest, 20% grassland, 10% bare rock". Forest density is just the forest fraction, and it varies within a climate zone by a km-scale patchiness noise, so woods have thick and thin parts without any new data.

The potential cover at a point is `cover(substrate, climate, patchiness)`. The actual cover is the potential unless a **deviation** covers the point — a clearing, a field, a planted wood — in which case it is the deviation's state at the current time: a clearing lowers the forest fraction and raises grassland, rather than flipping a type.

---

## Why

- Keeps the map a function ([[Technical/Globe Viewer]]): a whole planet of ground and plants costs nothing to store.
- Makes change cheap and local: cutting a forest stores one deviation, not a repaint of the world.
- Regrowth is an event: a deviation's state is a known curve from its start time, so "this clearing becomes scrub" is a computed moment ([[Design/Event-Driven]]), invalidated if something else happens there.
- Deviations are map objects with a place, an owner and a time ([[Design/Map-Centric]]).

---

## Decided

- Substrate type is static. Any *quality* of it that gameplay needs (fertility, drainage) is a deviation, same mechanism as vegetation.
- Mixtures, not classes. The simulation reads fractions (`terrain::mixtureAt`), rendering blends the same fractions, and the tooltip lists them. A hard class is never stored; if a mechanic needs one it takes the dominant member.

---


**Biome balance**: plain forest is the default tree cover — it takes most of most worlds. Rainforest needs warmth *all year* — the gate is the coldest season's temperature (Köppen-style, ~16–20 °C), not the annual mean, so a monsoon highland with warm summers and 9 °C winters gets forest, never rainforest — and truly wet ground (roughly 7 mm/day at tropical evaporation); marsh is a local patchy feature of extreme waterlogging, broken up by the tree-density noise, never a regional biome.

**Waterlogging**: the climate's water balance (rain minus evaporation) is an input to the mixture — waterlogged flat ground pulls toward mud substrate, and marsh cover follows. Bog country instead of endless ponds.

## Open Questions

- Shape of a deviation: cells on a fine sparse grid (recommended — cheap to query, the GPU reads it like hydrology), polygons, or brush strokes?
- Regrowth curves per vegetation class and per substrate.
- Rain shadow and distance-from-sea in the moisture field, now that mountain belts exist.
