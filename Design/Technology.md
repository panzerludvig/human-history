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

## Discovery: need invents necessity; serendipity covers the rest

Two modes (decided 2026-08-26; this supersedes the original fixed-pace world clock for necessity techs, resolving the old open question "a harsher world does not discover sooner" the other way):

**Need-driven** (farming, granaries — nobody would do these unless they had to):

- Each unaware settlement contributes a need weight = sustained-state ramp × suitability s × min(P/300, 3). The **sustained ramp** is the core rule: the state must have held for a full year to count at all and saturates at four years — one tough winter changes nobody's lifestyle, but hunger year after year drives desperation. Farming's state is sustained hunger — a genuine shortfall, φ < 0.92 (the overshoot trough reaches ~0.85), not the ~0.98 comfort glide the split rule watches, tracked by its own `hungrySince` which unlike the split timer is *not* reset by sending out a band (emigration doesn't cure desperation); granaries' is the storage fill signal (filled in the fat season, nearly drained in the lean one) holding in consecutive years — the same signal that triggers building for those who already know how.
- The world invention rate = **√(Σ need) / 2,000 years**, and when the clock fires the inventor is drawn proportional to need (mathematically this is per-settlement chances with sublinear crowding). The square root is the tuning lever: more potential inventors do invent sooner, but a crowded hungry world does not invent everything instantly. A fully comfortable world never invents these at all — no farming in Eden.
- The clock re-checks every 5 years even at zero rate, since need can arise between draws.

**Serendipity** (husbandry; the default for future opportunity techs): the original world clock. Rate = (share of world population that does not yet know it) / 10,000 years — population-invariant pacing; inventor picked by weight P · s · (1 + 9·scarcity). As a technology spreads, independent invention fades toward zero in both modes; early on, several independent cradles can appear.

This is a client of [[Design/Event-Driven]]: one scheduled event per technology, weights evaluated lazily at fire time, rescheduled when adoption changes the pool (exact — each adoption is a discrete event) or on the 5-year need horizon (piecewise-constant approximation of a yearly-drifting rate).

### Expected mean time to discovery (calibration table)

Pinned so future technologies can be tested against drift — when adding or tuning anything that touches these formulas, re-derive this table and re-check it (`test_gran.cpp` prints the live need sums and implied means). All draws are exponential (median ≈ 0.69 × mean); for need techs with total weight W, mean = 2,000 / √W years.

| Scenario | Weight W | Expected mean |
|---|---|---|
| Farming: one settlement, P ≥ 300, s = 1, hungry ≥ 4 yr | 1 | 2,000 yr |
| Farming: same but s = 0.1 (real good sites are ~0.05–0.16) | 0.1 | 6,300 yr |
| Farming: 100 settlements each at weight 0.1 | 10 | 630 yr |
| Granaries: one farming settlement, P = 300, fill signal ≥ 4 yr (s = 1) | 1 | 2,000 yr |
| Granaries: same but non-farming forager (s = 0.15) | 0.15 | 5,200 yr |
| Husbandry (serendipity): pristine world, any population | — | 10,000 yr |
| Adoption: aware settlement, one fully-expert neighbour, s = 1, fully hungry | — | 100 yr |
| Adoption: same but comfortable (φ ≥ 1.11) | — | never (re-checked 5-yearly) |

---

## Spread: proximity, in two layers

Knowing about a technology and practising it are different states — ideas travel where practice can't. Each settlement is **unaware → aware → practising**:

- **Awareness** spreads by contact, ungated by terrain: hearing about farming needs neighbours who know of it, not soil to plant. Rate = Σ(knowing neighbours) / 25 years within 160 km (twice the minimum settlement spacing); aware-but-not-practising settlements count as sources, so knowledge relays across tundra and desert belts. A modern tundra settlement knows exactly what farming is — it just has nowhere to do it.
- **Practice** is learning the craft: requires awareness, s > 0, practising neighbours to learn from — and a reason. Rate = s · **adoption need** · Σ(neighbour expertise) / 100 years within the same radius. Adoption need (decided 2026-08-26): nobody changes a working lifestyle, but you needn't be desperate either — getting utility is enough. A settlement expanding at its maximum rate (φ ≥ 1.11, where growth saturates) adopts at rate zero; the rate ramps up as food starts to bind, reaching full speed at the invention-hunger threshold (φ ≤ 0.92) — the hungrier, the faster. Granaries use their own utility signal (the fill cycle binding) instead of φ. So the ~100-year mean with one expert neighbour holds for a genuinely hungry settlement; a comfortable one waits until it isn't. Expertise accrues only here. Invention puts a settlement straight into practising.

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
| Granaries | undiscovered | built granaries | settlement storage |

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

## Granaries (the third technology; the first building)

Granaries do not require farming — pre-agricultural wild-grain stores are
real — but farming makes the need obvious: a practising farmer's suitability
is 1.0 against a 0.15 base, so farmers are ~7x likelier to invent granaries
and ~7x faster to adopt them (decided 2026-08-26; the gate was briefly
hard). The granary itself is the project's first **building** — a structure
that exists on the map at the settlement, visible when zoomed right in,
identified by the tooltip, and acting as a local modifier: each one banks a
fixed absolute store (10,000 rations), so its worth in days shrinks as
people multiply and granary count naturally tracks population.

**Demand is measured, not planned**, from the annual fill cycle. A build
starts only when, over the past year, the settlement both *filled* its
existing capacity (the fat season had more to bank) and then *nearly drained
it* (fill fell below 35% — the buffer binds, famine was near). Once capacity
comfortably covers the winter drawdown the low mark stays high and building
stops; growth deepens the drawdown and reopens demand. Both sides of the
gate matter: a settlement that cannot fill what it already has gains nothing
from more, so it never builds more. Mild-climate foragers essentially never
qualify — their year has no deep lean season.

**Building**: a fixed work total (1,000 man-days — the total never changes),
delivered by ~2% of the people, at a pace set by granary expertise and by
local materials (standing timber, else bare rock; never zero — driftwood and
fieldstone exist everywhere, just slowly). Only the fed build: work pauses
when stores fall to the hoarding threshold. A skilled, wooded village raises
one in a season or two; a novice one on bare steppe takes years.

Storage capacity replaced the old farming-expertise stand-in (cap was 90–180
days via the farm multiplier; it is now 90 days + built granaries).
Deferred to a follow-up: maintenance and decay (abandoned granaries should
crumble back to ruin).

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
- ~~The world-clock model means a harsher world does not discover sooner — pacing is fixed by design. Revisit if that ever feels wrong in play.~~ Revisited 2026-08-26: necessity techs (farming, granaries) are now need-driven; see Discovery above.
- ~~Should *adoption* (aware → practising) also be need-scaled?~~ Decided 2026-08-26: yes — see the practice rate under Spread.
