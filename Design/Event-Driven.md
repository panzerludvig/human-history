# Event-Driven

**Status:** Concept — see [[Meta/Status Vocabulary]]

Core concept: as far as possible, the simulation works by events, not by polling. Nothing periodically checks whether something has happened; instead, the moment something *will* happen is computed and scheduled, or a condition is attached as a trigger and evaluated only when the things it depends on are changed.

---

## The Rule

Two ways a thing may be detected, in order of preference:

1. **Compute the moment.** If the future can be calculated from current state — two objects on known trajectories will meet at time T — schedule an event at T. If the state it was computed from changes, the event is invalidated and recomputed.
2. **Trigger on manipulation.** If the moment cannot be computed, attach a condition to the objects involved. The condition is checked only when one of those objects is manipulated — moved, created, destroyed, changed — never on a timer.

Periodic comparison ("every tick, compare all positions") is not an option.

### Example: collision

Two objects A and B. When either is given a trajectory, the intersection time is computed and a collision event scheduled. If A changes course before then, the scheduled event is cancelled and recomputed. If trajectories are not closed-form (say, one object follows terrain), the fallback is a trigger: whenever A or B is moved, check whether they now overlap.

---

## Why

- No fixed tick means no wasted work when nothing is happening and no lost precision when a lot is. Time can advance straight to the next event.
- Scheduled events make the future legible: the simulation knows what is going to happen and when, which is exactly what an autonomous actor — or the player — needs in order to plan.
- It forces every mechanic to state its cause explicitly. A mechanic that can only be expressed as "check every so often" is a sign it is not yet understood.

---

## Consequences

- The core technical structure is a priority queue of timed events plus a dependency mechanism for invalidating them. See [[Technical/Architecture]].
- Every object that can change needs to know which scheduled events and triggers depend on it.
- Continuous processes (growth, decay, travel) must be expressed as "state at time t is f(t)" with a scheduled event at the moment something discrete happens, rather than incrementing a value each tick.

---

## Open Questions

- How does time advance between events when the player is watching — does it animate toward the next event, or jump?
- What does the player's own input look like as an event? Does the simulation pause waiting for input, or does it never wait?
- Invalidation granularity: when an object changes, which dependent events are recomputed — all of them, or only those whose inputs actually changed?
- How are triggers that depend on *many* objects (e.g. "anything enters this region") kept from degenerating into polling?
