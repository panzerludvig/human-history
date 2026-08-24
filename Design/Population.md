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

- Growth: up to +2.8%/yr when food per head is ample, down to −7%/yr in famine, proportional to the surplus/deficit (saturating quickly, so growth runs near its maximum whenever there is real surplus).
- Land: regenerates toward 1 over ~33 years; is depleted in proportion to use (~22-year timescale at P = K). Slow land feedback against fast growth is what makes the overshoot pronounced: ~18% above the sustained capacity, peaking around year 33 from a half-capacity start.

P can overshoot because R gives way slowly, then falls back as R runs down; depending on how hard the overshoot ran, it settles or oscillates. Depleted land that outlives its people is the beginning of "the world remembers".

**Calibration**: the yield table is *observed sustained* density, so it is what settlements settle at, not a ceiling they never reach. Internally the pristine ceiling is table ÷ R*, where R* = 0.549 is the land condition at which regeneration balances depletion; the displayed capacity is the sustained figure.

---

## Time

No ticks. Each settlement integrates its own P and R forward at scheduled re-evaluations, choosing the next moment as "when will my state have drifted ~5%" (clamped 1 month–5 years). A settlement at equilibrium wakes rarely; one in collapse wakes often. This is the first real client of [[Design/Event-Driven]].

---

## Deferred

- Farming, herding, fishing: raise yield at the cost of labour (10 h/person/day, currently unspent). How they are discovered is [[Design/Technology]].
- Splitting and migration: when R runs out, leaving is rational — the split rule for actors.
- Named individuals: actors of size 1, instantiated on prominence.
- Fresh vs salt lakes; irrigation.

---

## Open Questions

- Settlement placement is static at world creation; new settlements should found themselves where K is unclaimed.
- What does a settlement look like on the map as it grows — a dot, a painted footprint, a cleared deviation?
- Interaction radius between neighbouring settlements (competition for the same foraging ground).
