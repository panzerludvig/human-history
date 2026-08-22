# Core Concepts

**Status:** Stub — see [[Meta/Status Vocabulary]]

The concepts the game is built from. Per [[Meta/Incremental Design]], a concept is listed as **Core** only if the current playable increment is incomplete without it. Everything else is a **Candidate** — named so it isn't forgotten, but not designed.

---

## Core

| Concept | Description | Status |
|---------|-------------|--------|
| _none yet_ | | |

---

## Candidates

Carried over from Delegate as ideas, not decisions. Each becomes a design note only when promoted to Core.

| Candidate | One-line idea | Origin |
|-----------|---------------|--------|
| Autonomy | Subjects act on their own; the player guides rather than controls | Delegate |
| Delegation | The player acts through intermediaries who task others | Delegate (Administrators) |
| Map-centric | Everything exists on the map; the map is the interface | Delegate |
| Event-driven time | No fixed tick; time advances via events | Delegate |
| Agents | One general entity type for anything that holds goals and acts | Delegate |
| Objects by property | Things defined by properties rather than fixed types | Delegate |
| Emergence | Few simple rules, complex outcomes | Delegate |

---

## Promoting a Candidate

1. Show which desired end state in [[Design/Overview]] cannot be produced without it.
2. Create `Design/<Concept>.md` with status Concept.
3. Move the row to the Core table and log the reasoning in [[Dev Log/Log]].
