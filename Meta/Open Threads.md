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
**2026-09-07, the two-layer quasi-geostrophic model (`qg2.h`, flag `QG2`, off):** the second dynamic layer exists and runs on Earth. A two-layer QG model in two channels (20°–80° each hemisphere) relaxes its thickness toward the painted surface climate and hands its lower-layer wind to the water; a year takes about a minute of the sweep. Half-hour steps, potential-vorticity inversion by Fourier transform in longitude and a tridiagonal solve in latitude, an Arakawa Jacobian, third-order Runge-Kutta, a polar filter at the advection limit, a free-slip poleward wall for the mean flow. On Earth (`sweep.exe earth 0 qg 1`) it gives an Earth-like general circulation: eddy energy 17–30 across 40°–60° in both hemispheres, an upper jet of 16–21 m/s, surface westerlies of 3–8, trades of 5–9, a 1013 hPa subtropical high, and west-coast rain that rose toward Earth's (Pacific NW 2.5 vs 3.5 mm/day; W Europe 2.6 vs 2.2). What decided the amplitude was the thickness target: the painted surface temperature puts the front at 70° and the eddies with it; a column temperature from a lapse rate that falls off both ways from 15 °C (moist adiabat on the warm side, inversion on the cold) with a gain of 1.5 brought the storm tracks to 40°–60°. That gain is the least justified constant in the model. Regressions to fix before it can replace the belts: the Andes and Cascades rain shadows leak under the stronger, gustier wind (Patagonia 6.3 vs 0.6 mm/day; Great Basin 1.8 vs 0.7), and the US Midwest lost its summer Gulf inflow (0.8 vs 2.5), which the painted thermal anomaly wind gave and the QG lower layer does not. Harness: `build_testqg.bat` → `test_qg2.exe days printEvery [blobK]`; the trace `HH_QG_DEBUG=1` prints the daily wind maxima and stops at the first non-finite value.

### Terrain overlay (modifications)
**Undecided:** How the terrain function gains mutable state — a dug channel, a dike, a flooded valley. Leading idea: `height(p) = base(p) + overlay(p)` where the overlay is sparse data (the hydrology layer is the template). Needs a resolution choice and a rule for how the GPU samples it.
**Would settle it:** One modification mechanic that needs it, implemented end to end.

### Tides
**Undecided:** Sea level as `seaLevel(p, t)` instead of a constant: a lunar/solar bulge is a few trig calls per pixel. Heights are now in metres so a 2 m tide is representable. Depends on time existing in the simulation.
**Would settle it:** The event-driven time model — tides are the first natural "computed moment" (when does this mudflat flood?).

### What is the first playable increment?
**Undecided:** The smallest thing that can be played and judged — what the player does, what changes, what "a turn" or "a moment" is.
**Would settle it:** A one-paragraph description in [[Design/Overview]] plus the 2–4 concepts it needs in [[Design/Core Concepts]].
