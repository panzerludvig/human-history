# Spherical World

**Status:** Concept — see [[Meta/Status Vocabulary]]

Core concept: the map is a sphere, Earth-like in scale and character — continents, oceans, poles, a day/night cycle are all plausible consequences. There is no edge and no wraparound seam.

---

## The Rule

- Positions are points on a sphere. The coordinate system must have no singularities that matter for gameplay (plain latitude/longitude is awkward at the poles; unit vectors or a cube-sphere/geodesic scheme are alternatives).
- Distance is great-circle distance; direction is along a geodesic.
- "Earth-like" is the default: similar radius, similar land/ocean proportion, climate zones by latitude. It is not a requirement to reproduce Earth itself.

---

## Why

A sphere removes the hardest artificial constraint a strategy map has — the edge — and makes global scale real: reaching the other side of the world costs what it costs. Earth-like means intuition carries over: players already know what a pole, an equator, and a long sea voyage mean.

---

## Consequences

- [[Design/Map-Centric]]: every position is a point on the sphere or a region of it.
- [[Design/Event-Driven]]: trajectories are great-circle arcs (or paths built from them); meeting times are computed on the sphere.
- Rendering and pathfinding both need a spherical spatial index before anything else is built.

---

## Open Questions

- Coordinate representation: unit vector, quaternion, lat/long, or a discretised geodesic grid with continuous positions within cells?
- Is the surface continuous, or is there an underlying grid for terrain?
- Does the first playable increment need a full globe, or can it run on a sphere with trivial terrain?
