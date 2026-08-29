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

## Demography

Decided 2026-08-29. People are four stocks, not a headcount: **children, adult men, adult women, elderly**. Every share is emergent — nothing anywhere enforces a ratio.

- **Births come from the women**, at a replacement rate of ~0.10 per woman per year, multiplied by food: at full surplus fertility is ~2.35x replacement, which is what population growth now *is*. There is no growth term applied to a headcount any more.
- **Children take fifteen years** to reach adulthood and about a third never do (3.6%/yr child mortality) — the single biggest brake on growth, and a lever for later technologies to move.
- **Adults** live a 45-year working life at 1%/yr mortality before ageing into the elderly stock.
- **The elderly die quickly** (25%/yr, a few years past sixty), which is what keeps them scarce without any cap.
- **Famine takes the weak first**: deaths are handed out by vulnerability (children 1.5, adults 1.0, elderly 2.0), so a hungry settlement loses its next generation before it loses its workers.

Measured in a test world: at year 50, mid-expansion, 42% children / 27% men / 26% women / 4% elderly — young, as a fast-growing population should be. By year 1000, near equilibrium, 34% / 31% / 30% / 5%. The child share falling as growth slows is the structure responding to circumstance, which is the point of tracking it.

`P` remains as the sum of the four, kept in step, so everything that only cares about headcount is unchanged. **Known deviation**: children are unsexed, so an overall "51% women" cannot be stated; among adults the split is ~51% male, since boys are 51.2% of births and adult mortality is currently equal. Differential adult mortality would tip it, and is a one-constant change if wanted.

Deferred: children eating less than adults (the yield table is calibrated in whole people), and the demographic effect of anything other than famine and raiding.

## Time

No ticks. Each settlement integrates its own P and R forward at scheduled re-evaluations, choosing the next moment as "when will my state have drifted ~5%" (clamped 1 month–5 years). A settlement at equilibrium wakes rarely; one in collapse wakes often. This is the first real client of [[Design/Event-Driven]].

## Wild game: a shared, slow, mortal pool

Decided 2026-08-26. Food capacity splits into two pools with different physics. Gatherable plants stay per-cell with the fast-recovering land condition R. The **game-borne share** of each cover's yield (grass, steppe, and tundra feed people almost only through animals; forests partly) instead tracks a **regional pool** on the climate grid (~200 km): every settlement in the region hunts the same herds.

Three asymmetries make it the mammoth story rather than a second R:

- **Slow**: recovery on a lifetime scale (80 yr), depletion at capacity-draw in 25 yr — the pool responds to generations of pressure, not seasons.
- **Hunted harder when scarce**: the take falls only as √health (hunters range wider for scarcer game), which is also what lets a pool be pushed past saving instead of being left alone.
- **Mortal**: below the extinction floor (0.15) recovery stops entirely; the floor deliberately sits *above* the starvation stall (~0.11, where the hunters' own famine caps the pressure), so a pool driven that low keeps sliding to zero — permanently. The old way of life does not come back.

A lone settlement dents its region (equilibrium ~0.9); dense colonization drags it toward half; concentrated pressure — small island pools first, later farming-fed populations that keep hunting — pushes past the floor. Regional game collapse produces sustained hunger across many settlements at once, which is exactly the need signal that invents and adopts farming and husbandry: the crisis creates agriculture, as it did. In an 800-year test world: ~1,100 regions dented, ~10 below half, 3 extinct.

**Only the big game is pooled** (2026-08-28). The animal share of a cover's yield splits again, by `SMALL_SHARE`: herd country (tundra, steppe, grass) is 85% big game, forest and marsh only 40-50%. Big game draws on the regional pool; small game lives on the local land condition R, which is already the fast-recovering, use-depleted resource small game actually is, and is gated on bows instead ([[Design/Technology]]). Without that split a pool collapse took every animal calorie in a 200 km region at once; with it, the woods keep a fallback and the steppe does not, which is both the historical pattern and the reason archery matters.

Pools update on a fixed 90-day world event (step-size invariant, O(settlements) per tick); each settlement caches its pool's health. Bands' forage scales with the pool but their draw is ignored (too small and transient). Panels show "Wild game: N%" where game matters; the tooltip appends "game N%" once a region is visibly dented. Deferred: mobile herds as visible, followable entities (routes, seasonal interception) — this pool becomes their population when they arrive.

## Daily rhythm

Decided 2026-08-26 (`src/daylight.h`). People sleep at night and work, move, and eat by day — emergent from three ingredients rather than hardcoded hours:

- **Sleep is a biological constant** (~7 h), so at most 17 waking hours — the polar-summer work cap falls out by itself.
- **Light is an economic input**: daylight is free and full-efficiency; darkness can be worked by firelight at ×0.35. Bands travel by any light good enough to walk (through civil twilight — arctic winters keep a usable glow after the sun stops rising).
- **Need buys the margin**: the firelight extension is proportional to hunger (the same φ ramp adoption uses, content 1.11 → desperate 0.92). A content settlement stops at sunset; a hungry one burns torches — the "winter minimum" emerges only for those who need it.

The gather budget (1.5 rations/person/day) is the 12-hour baseline scaled by the day's effective hours; annual-mean daylight is 12 h at every latitude, so mid-latitude calibration is untouched while seasonal amplitude grows toward the poles. Band speed (15 km/day) is likewise the 12-lit-hour baseline. Sub-day time steps see the rhythm — a band stands still in the dead of night, stores hold flat till dawn — while whole-day steps use daily totals, which already integrate it. The sun's declination and phase are defined once in daylight.h and shared with the renderer, so the lit hemisphere on screen and the hours people work can never drift apart.

Deferred: weather-dependent light, fuel as a resource, hot-climate siesta patterns, night-time visuals (fires — a settlement glow was built and reverted 2026-08-26, commits ba131ff/e6867b4, because the look wasn't right even when very faint; get back to it with a better treatment, perhaps only at close zoom or as sparse individual points rather than a halo).

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
