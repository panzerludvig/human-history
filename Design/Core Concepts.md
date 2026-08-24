# Core Concepts

**Status:** Concept — see [[Meta/Status Vocabulary]]

The concepts the game is built from. Per [[Meta/Incremental Design]], a concept is listed as **Core** only if the current playable increment is incomplete without it. Everything else is a **Candidate** — named so it isn't forgotten, but not designed.

---

## Core

| Concept | Description | Status |
|---------|-------------|--------|
| [[Design/Map-Centric]] | Everything exists on the map; every physical object has a position | Concept |
| [[Design/Event-Driven]] | No polling; moments are computed and scheduled, or triggers fire on manipulation | Concept |
| [[Design/Spherical World]] | The map is a sphere, Earth-like; no edges | Concept |

These three are foundational rather than feature-level: they constrain what every later mechanic must look like, so they are decided before the first increment is chosen rather than derived from it.

---

## Design notes

Mechanics built on the core concepts, each with its own note:

| Note | Description | Status |
|------|-------------|--------|
| [[Design/Terrain]] | Layered terrain: height, substrate, climate, vegetation; only deviations are stored | Concept |
| [[Design/Population]] | A carrying-capacity field; settlements as sparse actors with overshoot dynamics | Concept |
| [[Design/Technology]] | Dark, shared, automatic discovery driven by need; expertise; farming first | Implemented |
| [[Design/Migration]] | Bands as moving settlements; storage; emergent nomadism; the first agent | Implemented |
| [[Design/Weather]] | Two-layer atmosphere run at generation; stored climatology; weather as a function | Designed |

---

## Candidates

Carried over from Delegate as ideas, not decisions. Each becomes a design note only when promoted to Core.

| Candidate | One-line idea | Origin |
|-----------|---------------|--------|
| Autonomy | Subjects act on their own; the player guides rather than controls | Delegate |
| Delegation | The player acts through intermediaries who task others | Delegate (Administrators) |
| Agents | One general entity type for anything that holds goals and acts | Delegate |
| Objects by property | Things defined by properties rather than fixed types | Delegate |
| Emergence | Few simple rules, complex outcomes | Delegate |

---

## Promoting a Candidate

1. Show which desired end state in [[Design/Overview]] cannot be produced without it.
2. Create `Design/<Concept>.md` with status Concept.
3. Move the row to the Core table and log the reasoning in [[Dev Log/Log]].
