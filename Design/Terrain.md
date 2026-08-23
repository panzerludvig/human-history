# Terrain

**Status:** Concept — see [[Meta/Status Vocabulary]]

Core concept: terrain is a stack of layers, each a function of the layers below it, and only deviations from those functions are ever stored.

---

## Layers

| Layer | What it is | Changes | Stored |
|-------|-----------|---------|--------|
| Height | Metres above sea level — plates, continents, mountains | Never (until the overlay exists) | Nothing; seed |
| Substrate | What the ground is made of: soil, sand, rock, scree, silt, mud, ice | Geologically; never in play | Nothing; function of height, slope, climate, rivers |
| Climate | Temperature and moisture | Slowly, not by the player | Nothing; function of latitude, altitude, noise (rain shadow later) |
| Vegetation | What grows: tundra, taiga, forest, rainforest, grassland, steppe, savanna, shrub, marsh, desert | By the player and by time | Deviations only |

The potential vegetation at a point is `vegetation(substrate, climate)`. The actual vegetation is the potential unless a **deviation** covers the point — a clearing, a field, a planted wood — in which case it is the deviation's state at the current time.

---

## Why

- Keeps the map a function ([[Technical/Globe Viewer]]): a whole planet of ground and plants costs nothing to store.
- Makes change cheap and local: cutting a forest stores one deviation, not a repaint of the world.
- Regrowth is an event: a deviation's state is a known curve from its start time, so "this clearing becomes scrub" is a computed moment ([[Design/Event-Driven]]), invalidated if something else happens there.
- Deviations are map objects with a place, an owner and a time ([[Design/Map-Centric]]).

---

## Decided

- Substrate type is static. Any *quality* of it that gameplay needs (fertility, drainage) is a deviation, same mechanism as vegetation.
- Classes are hard for the simulation (an enum at a point) and blended for rendering; both are cut from the same continuous variables so they agree.

---

## Open Questions

- Shape of a deviation: cells on a fine sparse grid (recommended — cheap to query, the GPU reads it like hydrology), polygons, or brush strokes?
- Regrowth curves per vegetation class and per substrate.
- Rain shadow and distance-from-sea in the moisture field, now that mountain belts exist.
