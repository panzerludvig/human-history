# Weather

**Status:** Implemented — see [[Meta/Status Vocabulary]]

Weather is a two-layer atmospheric model run **once, at world generation**, and stored as a climatology — a per-place, per-season probability function for rain, wind, cloud, and temperature swing. At runtime, weather is a pure function: climatology instantiated with seeded noise. Climate is the time-average of weather; since nothing perturbs it year over year, simulating it live would only re-derive the same statistics.

---

## What implementation changed (lessons)

Three deviations from the design as first written, each forced by the model blowing up or misbehaving:

- **The upper layer is implicit, not prognostic.** Integrating momentum at this grid and step was numerically unstable; winds are now the *diagnostic* Ekman balance (friction + Coriolis + pressure gradient), bounded by construction. Convergence still lifts, divergence still sinks — the overturning survives as the design intended.
- **Heat moves by diffusion only.** Advecting temperature with the surface wind refrigerated every heat low: the convergent branch imports cool air, and the upper return flow that closes the energy loop is not modelled. A large eddy diffusivity stands in for the whole poleward heat transport (Budyko-style). Moisture *is* advected by the winds — that is what shapes the rain map.
- **Rain comes from a motion-modulated capacity.** Moisture piled up wherever air was cold (148 mm/day at a pole, 0.2 in the tropics). Fix: flux-form moisture transport plus an effective capacity that shrinks under uplift and swells under subsidence — convective and orographic rain where air converges or climbs, dry descent zones and lee sides.

Accepted at: correct zonal structure (wet tropics ~3 mm/day, subtropical minimum, dry poles), monsoon-like seasonal migration of rain onto summer continents, wet tropical coasts with dry continental interiors, and an emergent Antarctica analogue (continental pole −54 °C vs oceanic pole −5 °C). Generation adds ~12 s (OpenMP) to world build.

## The generator: a two-layer atmosphere

One layer cannot represent overturning circulation, which is most of what weather is. Two can:

- **Columns** hold low-layer temperature deviation (from the [[Design/Terrain]] climate baseline — weather is a deviation layer, only deviations are stored) and low-layer moisture; the high layer carries heat and return flow only, since moisture physically lives in the bottom ~2 km.
- **Horizontal flow** from pressure proxies: a warm column has low surface pressure — surface air converges on it, upper air spreads out. Cooling reverses it. This is the expansion/contraction idea done properly: both directions at once, at different heights.
- **Vertical motion** from buoyancy plus terrain forcing (low-level wind into a slope is pushed up).
- **Moisture** follows the stock-and-capacity shape used for food stores: capacity from temperature (roughly doubling per 10 °C — Clausius-Clapeyron), evaporation filling toward capacity over water (faster when warm), and **rain when rising or cooling air's capacity drops below its content**. Rain is a function of uplift — its true cause.
- **Thermal inertia** per cell from the cover mixture: water barely swings between day and night, forest damps the swing, bare desert swings hard. This is what forests actually do to temperature.
- **Coriolis**: a latitude-dependent rotation on the wind acceleration. With it, the model should produce trade winds, westerlies, equatorial uplift (the wet tropics), and subtropical descent (the desert belt) on its own. Fallback if emergence misbehaves: prescribe latitude-band prevailing winds and keep the rest.

Expected emergent outcomes, which double as the acceptance test: wettest belt at the equator, deserts under the subtropical descent, orographic rain with lee-side rain shadows, sea breezes, monsoon-ish seasonal coast rain, cold clear desert nights.

---

## The proxy: climatology

Integrated a couple of sim-years after spin-up, accumulating per cell (coarse grid, ~256×128) and per season:

- rain probability and mean intensity
- prevailing wind vector (low level)
- cloudiness
- seasonal mean temperature
- diurnal temperature swing amplitude

Seasons interpolate; the diurnal cycle is applied analytically from the sun ([[Technical/Globe Viewer]]) and the stored swing amplitude — it needs no storage.

---

## Runtime: weather as a function

Weather at (point, moment) = climatology(cell, season) instantiated with seeded noise fields drifting with the stored wind: storm blobs that hit each cell at its stored frequency and intensity. Individual days differ; the pattern holds. No simulation state, nothing in save files, no coupling to the clock — a +100-year jump costs nothing, and any event-driven consumer ([[Design/Population]], future agents) can query rain at an exact moment or sample "when will it next rain here".

---

## Recomputation

The climatology is a derived layer like hydrology: constant until something genuinely changes its inputs (deforestation at scale, a new sea), then recomputed — regionally if possible. "The weather pattern shouldn't change unless something would change it" is the contract.

---

## The unification prize (deferred)

Both painted climate fields retire once the generator's output is validated — as one milestone, because everything downstream consumes them as a pair:

- **Moisture**: `moistureAt` is a painted noise field with a hardcoded subtropical dry band. The generated rainfall map replaces it: vegetation, yields, and rivers (runoff) then derive from rain the atmosphere actually delivered, and the desert belt exists because the circulation put it there.
- **Temperature**: `temperatureC(lat, h)` is a hand-drawn stand-in for the energy balance the generator computes anyway (insolation, albedo, radiation, heat transport). The stored seasonal means replace its latitude curve — gaining continental vs maritime climates, wind-warmed and wind-chilled coasts, and seasons, which the static field cannot express at all. The **lapse rate stays analytic**: the climate grid cannot see individual mountains, so stored temperature is at the model's smoothed elevation and the −6.5 °C/km correction for local height is applied at sample time, mirrored CPU/GPU like the other derived textures.

Two riders: vegetation ↔ albedo ↔ climate is a feedback loop — run the atmosphere against the painted fields as first guess, re-derive vegetation, run once more (doubles generation cost, converges fast). And a slab ocean gives maritime mildness but no currents — no Gulf Stream analogue, no anomalously warm high-latitude coasts; an honest limitation until ocean transport exists.

The swap reorders the generation pipeline (climate before vegetation, hydrology, population) and is not v1 — validate the rain and temperature maps against the painted fields first.

---

## Open Questions

- Generation cost: ~10–20 s of atmospheric integration on top of ~7 s of world build. Acceptable, or coarsen/shorten if it creeps.
- Lat-lon cells shrink toward the poles and break the stable step size there; standard fix is extra polar smoothing. Implementation hazard, noted.
- What the first visual is: clouds from cloudiness + moisture noise, rain as a darker veil, at the sun's mercy for lighting.
- Whether storm noise should be shared across neighbouring cells (storms as objects with tracks) or purely per-cell.
