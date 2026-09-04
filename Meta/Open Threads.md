# Open Threads

Parked topics and in-progress decisions — bigger than a quick note, not yet settled enough for a design note. Each entry states what is undecided and what would settle it. When picking up a thread, read the entry and the relevant Dev Log context before continuing.

---

<!-- format:
### Thread title
**Undecided:** ...
**Would settle it:** ...
-->

### Prognostic atmosphere and baroclinic eddies
**Undecided:** Whether the climate generator ever goes back to computing its own temperature and circulation. Parked 2026-09-04 in favour of prescribed temperature and wind with derived rain (see [[Design/Weather]]). The rebuild's physics — two-layer two-band radiation, sea ice as a mass, flux-form heat that conserves energy to 0.3 W/m2, a zonal energy-budget probe — lives on branch `conserving-atmosphere` and behind the `PRESCRIBED` flag in `atmosphere.h`. What it lacks is baroclinic eddies: without them the winter continent is 15–20 K warm, or the westerlies and the Antarctic go instead, depending on how the frontal proxy is gated.
**Would settle it:** Either a use for weather that only dynamics can give (storms that respond to the world's state rather than to rules), or a second dynamic layer with a ~10-minute step that produces travelling lows on the 200 km grid. The sweep's `PHYSICAL` score and the zonal probe are the acceptance test; the prescribed mode is the bar it has to beat.

### Terrain overlay (modifications)
**Undecided:** How the terrain function gains mutable state — a dug channel, a dike, a flooded valley. Leading idea: `height(p) = base(p) + overlay(p)` where the overlay is sparse data (the hydrology layer is the template). Needs a resolution choice and a rule for how the GPU samples it.
**Would settle it:** One modification mechanic that needs it, implemented end to end.

### Tides
**Undecided:** Sea level as `seaLevel(p, t)` instead of a constant: a lunar/solar bulge is a few trig calls per pixel. Heights are now in metres so a 2 m tide is representable. Depends on time existing in the simulation.
**Would settle it:** The event-driven time model — tides are the first natural "computed moment" (when does this mudflat flood?).

### What is the first playable increment?
**Undecided:** The smallest thing that can be played and judged — what the player does, what changes, what "a turn" or "a moment" is.
**Would settle it:** A one-paragraph description in [[Design/Overview]] plus the 2–4 concepts it needs in [[Design/Core Concepts]].
