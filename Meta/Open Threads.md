# Open Threads

Parked topics and in-progress decisions — bigger than a quick note, not yet settled enough for a design note. Each entry states what is undecided and what would settle it. When picking up a thread, read the entry and the relevant Dev Log context before continuing.

---

<!-- format:
### Thread title
**Undecided:** ...
**Would settle it:** ...
-->

### Terrain overlay (modifications)
**Undecided:** How the terrain function gains mutable state — a dug channel, a dike, a flooded valley. Leading idea: `height(p) = base(p) + overlay(p)` where the overlay is sparse data (the hydrology layer is the template). Needs a resolution choice and a rule for how the GPU samples it.
**Would settle it:** One modification mechanic that needs it, implemented end to end.

### Tides
**Undecided:** Sea level as `seaLevel(p, t)` instead of a constant: a lunar/solar bulge is a few trig calls per pixel. Heights are now in metres so a 2 m tide is representable. Depends on time existing in the simulation.
**Would settle it:** The event-driven time model — tides are the first natural "computed moment" (when does this mudflat flood?).

### What is the first playable increment?
**Undecided:** The smallest thing that can be played and judged — what the player does, what changes, what "a turn" or "a moment" is.
**Would settle it:** A one-paragraph description in [[Design/Overview]] plus the 2–4 concepts it needs in [[Design/Core Concepts]].
