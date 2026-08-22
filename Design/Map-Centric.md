# Map-Centric

**Status:** Concept — see [[Meta/Status Vocabulary]]

Core concept: as far as possible, everything in the game exists on the map. There is no separate layer where things "just exist" without a location.

---

## The Rule

- Every physical object has a position on the [[Design/Spherical World]]. A position is a point; where a point is meaningless (a stockpile, a settlement) it is a region, but it is still somewhere.
- Interactions happen on the map. Menus and panels are a last resort, not the default interface.
- Anything that cannot be given a position is an explicit, documented exception. The list of exceptions should stay short.

---

## Why

If everything has a position, then every interaction is a spatial question — what is near, what is between, what can reach what — and one set of spatial mechanics serves the whole game instead of each system inventing its own abstraction. It also makes [[Design/Event-Driven]] tractable: objects with positions have trajectories, and trajectories have computable meeting times.

---

## Open Questions

- What is the first thing that genuinely cannot be placed (money? knowledge?), and how is it handled?
- Point vs region: is a region a first-class kind of position, or a set of points?
- How much of the player's own interaction is "on the map" in the first increment — is there even a UI yet?
