# Technology

**Status:** Implemented — see [[Meta/Status Vocabulary]]

How settlements learn to do new things. The core ideas are inherited from Delegate's Tech Tree ([[Meta/Inherited from Delegate]]) — this is the first Delegate design concept re-examined and adopted. The first technology is farming.

---

## Principles (kept from Delegate)

- **Dark** — there is no visible tree. Nobody, player included, knows what could be discovered until it is.
- **Dynamic** — which technologies exist and when they appear varies between worlds.
- **Shared** — a discovery spreads between settlements; it is not owned.
- **Automatic** — discovery is never a menu choice. Settlements discover technologies themselves, driven by their circumstances. There is currently no player at all, so this is not just a principle but a necessity.

---

## Discovery: a world clock decides when, need decides who

Independent invention is a **world-level event**, not a per-settlement race. Per-settlement rates would sum — 300 settlements each on a 10,000-year clock discover somewhere within decades — so the pacing is pinned at the world level instead, and local conditions decide *who* invents rather than *when*:

- **When**: one exponential clock per technology. Rate = (share of world population that does not yet know the technology) / 10,000 years. In a pristine world the share is 1, so the mean time to first invention is exactly 10,000 years; as the technology spreads, independent invention fades toward zero — nobody independently invents farming once it is everywhere. Early on, while the share is still high, several independent cradles can appear.
- **Who**: when the clock fires, the inventing settlement is a weighted random pick among those that don't know it. Weight = P · s · (1 + 9·scarcity), where s is terrain suitability (below) and scarcity ramps 0→1 as food per head φ = K·R/P falls from 1 to 0.8. Big, hungry settlements on good farmland are the likely inventors — [[Design/Population]]'s overshoot phase is when a settlement is most inventive. Comfortable foragers don't invent agriculture; hungry ones do.

This is a client of [[Design/Event-Driven]]: one scheduled event per technology, weights evaluated lazily at fire time, rescheduled only when the non-knowing share changes (each adoption is a discrete event, so this is exact, not polled).

---

## Spread: proximity, in two layers

Knowing about a technology and practising it are different states — ideas travel where practice can't. Each settlement is **unaware → aware → practising**:

- **Awareness** spreads by contact, ungated by terrain: hearing about farming needs neighbours who know of it, not soil to plant. Rate = Σ(knowing neighbours) / 25 years within 160 km (twice the minimum settlement spacing); aware-but-not-practising settlements count as sources, so knowledge relays across tundra and desert belts. A modern tundra settlement knows exactly what farming is — it just has nowhere to do it.
- **Practice** is learning the craft: requires awareness, s > 0, and practising neighbours to learn from. Rate = s · Σ(neighbour expertise) / 100 years within the same radius — a mean of ~100 years with one fully-expert neighbour, faster with several, slower while the neighbourhood is still inexpert. Expertise accrues only here. Invention puts a settlement straight into practising.

Both draws are exponential and per-settlement, separate from the world clock; the clock's "non-knowing share" means *unaware*.

At 80 km settlement spacing the practice front reproduces the real diffusion speed of agriculture (~1 km/year across Neolithic Europe). Technologies therefore have geography: fronts that radiate outward from their points of origin ([[Design/Map-Centric]]), accelerate as the heartland's expertise matures, flow as knowledge across unsuitable belts and re-ignite as practice where s recovers on the far side — and stop at oceans, so other continents must wait for independent invention, giving separate agricultural cradles as Earth had.

The aware state is Delegate's "theoretical" stage arrived at from need rather than by adopting the full state machine — a partial, cheap resurrection of the deferred staging.

---

## Expertise

Knowing a technology is separate from being good at it (kept from Delegate). Each settlement holds an expertise level 0–1 per technology which scales the technology's effect. Inventor and adopter alike start at **0.2** and grow toward 1 with a **~50-year** timescale of practice. A settlement that just learned farming gains little; three generations later it is transformed — which answers Delegate's open question about how spread and expertise interact, and (since adoption rate is expertise-weighted) makes fronts spread slowly from a fresh cradle and faster from a mature one.

---

## The roster

Decided (2026-08-25). Tools, fire, stone weapons, and clothing are **not
technologies**: they are 100,000+-year-old universals for anatomically modern
foragers, already priced into the yield table, the winter hunting floor, and
cold survival.

| Technology | At start | Means | Effect |
|---|---|---|---|
| Farming | undiscovered | suitable land (sFarm) | food multiplier, harvest season |
| Animal husbandry | undiscovered | herd (living stock) | pasture-scaled flow, winter-proof |
| Fishing | known, expertise local | water access | food from water (planned) |
| Boats | known, expertise local | built boats | water movement, offshore fishing (planned) |
| Archery | known, low expertise | made bows | hunting/winter floor (planned) |
| Pottery | undiscovered | made pots | portable storage (planned) |
| Granaries | undiscovered | built granaries | settlement storage (planned) |

The general pattern: **benefit = knowledge x means**, where the means is
environmental (fishing, farming), made things (boats, bows, pots, granaries
-- Delegate's Items concept arriving in aggregate: equipment levels that are
built by labour, decay, and can be lost), or living stock (herds). Archery's
start-known status is a deliberate compression, recorded as such.

## Farming (the first technology)

**Terrain suitability s** (used by both discovery weight and adoption rate): the grass-like share of the surrounding cover mixture — grassland + savanna + steppe, with marsh at half credit (the real cradles were river floodplains) — times the farming climate window (warmth and moisture). Cereal agriculture came from wild grasses; you can't domesticate what doesn't grow around you. Grassland river valleys become the world's invention hotspots; tundra stays foraging country.

Farming multiplies the *food* side of the capacity min() by **1 + 4·s·expertise** — up to ×5 on prime grassland at full expertise, conservative for agriculture historically but right for its early form. Water is not multiplied, so where farming succeeds, water genuinely starts to bind — rivers begin deciding where the large settlements are. Irrigation will later multiply water use, so rivers start deciding where the large settlements are, through the same min() in [[Design/Population]].

Represented as an entry in a technology table (generalized when husbandry arrived), so the rest of the roster slots in beside it.

## Animal husbandry (the second technology)

One technology covers the household cow and the steppe flock: the scale is
emergent from the **pasture cap** (grass, steppe, savanna, some shrub and
tundra cover -- no warmth gate; reindeer are real). The herd is living stock
in people-fed units: seeded small at practice start (bred from wild capture),
growing logistically (~25%/yr) toward pasture x expertise, providing a food
flow that barely dips in winter -- **animals are walking stores**, which is
why pastoralism owns seasonal grassland. A small pasture-free farmyard bonus
covers scavenger animals (chickens, pigs). Discovery is weighted by pasture
and scarcity. Deferred: band-owned herds (pastoral nomadism proper),
overgrazing feedback on R.

---

## Future additions (deferred from Delegate, not discarded)

These were part of Delegate's Tech Tree design. Each is deferred because its prerequisite doesn't exist yet, not because it was rejected:

- **Investment** — the player lever: accelerating a technology's development by directing resources at it. Deferred until there is a player (or any deciding agent) to pull the lever. When guidance/autonomy arrives, this is the natural first point of contact between the player and technology.
- **Stages (theoretical → developed → improved)** — a technology's life as a state machine. The adopted model already covers the same ground informally: aware ≈ theoretical, practising ≈ developed, and expertise stands in for improvements (with unaware made explicit, which Delegate never had). The full staged model becomes worth its weight when technologies gain distinct practical applications that unlock partway (e.g. a theoretical technology that can be invested in before it works). Revisit together with investment.
- **Soft probability graph** — developing one technology shifts the discovery probability of related ones, giving a fuzzy graph rather than a fixed tree (and enabling Delegate's focus-area and catch-up-via-conflict effects). Meaningful once there are roughly ten technologies; with one it is pure overhead. The technology-table representation keeps the door open.

---

## Open Questions

- Does expertise decay if a technology goes unused (a collapsed settlement's knowledge)?
- Does farming change the map — cleared fields as terrain deviations ([[Design/Terrain]])?
- The world-clock model means a harsher world does not discover sooner — pacing is fixed by design. Revisit if that ever feels wrong in play.
