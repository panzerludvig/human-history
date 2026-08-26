# Population

**Status:** Concept — see [[Meta/Status Vocabulary]]

How humans exist in the world: a carrying-capacity field derived from the terrain, and sparse actors that deviate from it. Same pattern as [[Design/Terrain]]: functions by default, stored state only where something differs.

---

## The field

Carrying capacity K at a point is the minimum of two constraints, each "people supportable per day":

- **Food**: natural yield of the surrounding land (10 km foraging radius), from the cover mixture. People per km² at full land condition: forest 1.2, rainforest 1.0, marsh 1.0, grassland 0.8, savanna 0.6, taiga 0.4, steppe 0.3, shrub 0.2, tundra 0.08, desert 0.02, bare 0. A person needs 2200 kcal/day; the table is that, prefolded.
- **Water**: a person uses 20 L/day; a source supplies 5% of its discharge (from the drainage area and a runoff set by the moisture field). The sea supplies nothing. Land only; K = 0 on water.

Before farming, food binds almost everywhere and water is slack — historically right. Farming will raise food yield and irrigation will multiply water use; then rivers start deciding where cities are, via the same min().

---

## Settlements

Sparse actors at local maxima of K (at least 80 km apart, K ≥ 800). Each has:

- **P** — people. Not individuals; a count.
- **R** — condition of the surrounding land, 0–1. Effective food supply = K·R.

Dynamics (the overshoot the real world has — a stock between people and starvation):

- Growth: up to +2.8%/yr when food per head is ample, saturating quickly, so growth runs near its maximum whenever there is real surplus. Decline is famine through the food store — see [[Design/Migration]]: harvest fills a store, hoarding excludes people as it runs low, and deaths run on a starvation timescale. (The original smooth −7%/yr decline was replaced by storage; the calibrated overshoot behaviour was preserved.)
- **Seasons**: the land's food flow follows the seasonal climate. Foraging scales with a growing-activity factor (zero at freezing, full above ~12 °C) plus a 12% winter floor — what stays huntable under snow. Farming's annual total is delivered as squared activity: a prominent harvest season filling the granaries, degrading gracefully to year-round cropping in the tropics; farming also grows the store cap toward ~180 days (granaries). Strongly seasonal lands settle below their nominal annual capacity — the displayed capacity is scaled by the seasonal mean so the number matches where populations actually land. Wake-ups are scheduled from the *annual-mean* flow (the seasonal oscillation is recurring), but every time step brings every settlement and band current, so minute-by-minute stepping shows the world in full detail.
- Land: regenerates toward 1 over ~33 years; is depleted in proportion to use (~22-year timescale at P = K). Slow land feedback against fast growth is what makes the overshoot pronounced: ~18% above the sustained capacity, peaking around year 33 from a half-capacity start.

P can overshoot because R gives way slowly, then falls back as R runs down; depending on how hard the overshoot ran, it settles or oscillates. Depleted land that outlives its people is the beginning of "the world remembers".

**Calibration**: the yield table is *observed sustained* density, so it is what settlements settle at, not a ceiling they never reach. Internally the pristine ceiling is table ÷ R*, where R* = 0.549 is the land condition at which regeneration balances depletion; the displayed capacity is the sustained figure.

---

## Time

No ticks. Each settlement integrates its own P and R forward at scheduled re-evaluations, choosing the next moment as "when will my state have drifted ~5%" (clamped 1 month–5 years). A settlement at equilibrium wakes rarely; one in collapse wakes often. This is the first real client of [[Design/Event-Driven]].

## Daily rhythm

Decided 2026-08-26 (`src/daylight.h`). People sleep at night and work, move, and eat by day — emergent from three ingredients rather than hardcoded hours:

- **Sleep is a biological constant** (~7 h), so at most 17 waking hours — the polar-summer work cap falls out by itself.
- **Light is an economic input**: daylight is free and full-efficiency; darkness can be worked by firelight at ×0.35. Bands travel by any light good enough to walk (through civil twilight — arctic winters keep a usable glow after the sun stops rising).
- **Need buys the margin**: the firelight extension is proportional to hunger (the same φ ramp adoption uses, content 1.11 → desperate 0.92). A content settlement stops at sunset; a hungry one burns torches — the "winter minimum" emerges only for those who need it.

The gather budget (1.5 rations/person/day) is the 12-hour baseline scaled by the day's effective hours; annual-mean daylight is 12 h at every latitude, so mid-latitude calibration is untouched while seasonal amplitude grows toward the poles. Band speed (15 km/day) is likewise the 12-lit-hour baseline. Sub-day time steps see the rhythm — a band stands still in the dead of night, stores hold flat till dawn — while whole-day steps use daily totals, which already integrate it. The sun's declination and phase are defined once in daylight.h and shared with the renderer, so the lit hemisphere on screen and the hours people work can never drift apart.

Deferred: weather-dependent light, fuel as a resource, hot-climate siesta patterns, night-time visuals (fires).

---

## Deferred

- Farming, herding, fishing: raise yield at the cost of labour (10 h/person/day, currently unspent). How they are discovered is [[Design/Technology]].
- Splitting and migration: designed in [[Design/Migration]], along with storage — which will rewrite this note's decline dynamics (recalibration required).
- Named individuals: actors of size 1, instantiated on prominence.
- Fresh vs salt lakes; irrigation.

---

## Open Questions

- Settlement placement is static at world creation; new settlements should found themselves where K is unclaimed.
- What does a settlement look like on the map as it grows — a dot, a painted footprint, a cleared deviation?
- Interaction radius between neighbouring settlements (competition for the same foraging ground).
