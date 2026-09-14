# Resources

**Status:** Implemented — see [[Meta/Status Vocabulary]]. The first
increment (the ledger, heat, wood, the pile) is in code and measured;
what remains on paper is listed under Deferred below.

Resource gathering: the first needs beyond food, and the labour that meets
them. This is the prerequisite for trade (nobody trades when the only need is
calories, and everyone's calories are local), which is in turn the
prerequisite for the road to metal being worth travelling. Shaped in
discussion 2026-09-01.

---

## The labour ledger

Each settlement has a capped budget of man-days, set by population and the
fed/unfed state. Everything a settlement does — procuring food, gathering
fuel, building, crafting — is a fractional allocation of that budget,
recomputed when need changes (a client of [[Design/Event-Driven]]: evaluate
lazily, reallocate on the events that shift need). This promotes the
scattered labour constants that already exist — granaries take "~2% of the
people", bowyers are counted hands, "only the fed build" — into one explicit
ledger. Work is capped and allocated, never conjured: a settlement cannot
collect more of everything just because more kinds of thing are needed.

**No worker entities.** Settlements are aggregates, and the representation
is fractional allocation, not persistent tagged workers. "Most people are
foragers initially" is not a state anyone sets — it is what the allocator
outputs when food is the only binding need. Dedicated woodcutters are simply
the wood allocation becoming persistently nonzero. Occupational structure is
the readout of the allocation, not a mechanism. Discrete named specialists
become worth modeling when social structure or per-person expertise matters
— note the future interaction with the skill-loss model in
[[Design/Technology]], whose critical-practitioner counts (140 for a craft)
are claims about people, not shares.

**Priority stack.** Food up to need, then heat up to need, then construction
and crafts from the surplus. The existing "work pauses when stores fall to
the hoarding threshold" is already this stack with two levels; this
generalizes it. Consequence, wanted rather than feared: a hungry settlement
stops cutting wood, so famine in a cold climate compounds into a fuel crisis
— historically honest, and it gives cold-country collapse a distinct
signature.

---

## Needs are abstract; production has modes

A need is a demand (like food's); it is never inherently tied to one
resource. Burning wood is a *mode of production* for heat, not the need
itself — the same shape as food, whose one demand is met by a sum of modes
(foraging, big game, small game, fish, crops, herds), each gated by its own
means.

**Heat** is the second need: per person, scaled by cold —
heating-degree-days from the stored climatology ([[Design/Weather]]) —
covering fire and cooking. Its production modes:

- **Wood** — the dominant early mode, drawn from the wood stock below.
- **Dung** — via herds ([[Design/Technology]] husbandry), which is genuinely
  how pastoralists heated. Gives husbandry a second job beyond calories and
  keeps the treeless steppe habitable, as it historically was.
- **Peat** — the wet north. Later.
- **Charcoal, coal** — much later; charcoal is also the bridge to smelting.

Clothing and shelter remain priced-in universals (the roster decision of
2026-08-25). The need/mode split leaves the door open for them to later
*reduce* the heat need rather than supply it — insulation and fuel as
substitutes — with no redesign.

**There is no base per-capita wood need.** A modern human consumes wood
without producing any; that gap is consumption of things made elsewhere,
which only trade and specialization can represent. At this era the two
coincide (every household makes its own hafts and bowls) and the quantity is
a rounding error next to fuel, so nothing is modeled. When trade arrives, an
aggregate goods category — things made of wood, clay, hide — is plausibly
one of its tradable columns, and that is the moment the gap becomes
representable rather than fudged.

---

## Wood: a local, depletable stock

Derived from the surrounding forest cover ([[Design/Terrain]]), drawn down
by its consumers, regrowing on a ~50–100-year timescale — the same pattern
as the land condition R, only slower. Two mechanics already consume wood
implicitly (granary build pace: "standing timber, else bare rock"; bow
work: "wood gates the work"); they become draws on this real quantity, as
do firewood and future boat-building.

**Deforestation's real driver is farming, not gathering.** Fuel drawdown at
this era is small (correctly). Neolithic clearance was overwhelmingly
slash-and-burn for fields — burn the forest to plant, fuel nearly a
byproduct. So clearing is tied to farming practice: a practising farmer
converts forest cover toward open ground around the settlement, landing in
the terrain-deviation machinery, and cleared land locally *raises* farming
suitability, which gives clearing a purpose beyond destruction. This
resolves the open question in [[Design/Technology]] ("does farming change
the map — cleared fields as terrain deviations?"): yes.

---

## Byproducts cover the early game

A fraction of foraging labour yields wood and stone as a coupled output —
walking the land sweeps up deadfall and surface flint whether you meant to
or not. No choice is modeled. Dedicated gathering emerges when need outruns
the byproduct — concentrated farming populations, cold climates — which the
allocator notices as a deficit. No tech, no trigger.

**Stone** is wired into the same allocator but its dedicated allocation is
expected to sit at zero until quarried construction or scarce lithics exist
— a column that is always zero costs nothing and proves the generality.
Stone stays ubiquitous at a variable rate (the granary model's "never zero —
driftwood and fieldstone exist everywhere, just slowly"). Scarce
point-resources — obsidian, good flint, later copper and tin ore — are
deliberately deferred to arrive *together with trade*, as its founding
goods: unevenly distributed lithics are the archetypal first trade good
(obsidian is how Neolithic exchange networks are actually traced), and
placing them is a substrate question for [[Design/Terrain]] before it is a
technology question.

---

## Stockpiles without architecture

Wood gets an ambient stockpile like food's ambient 90 days. The stockpile is
forced by the physics — firewood need peaks in winter, gathering happens in
the mild seasons, so a pile must fill and drain — but a woodpile needs no
walls and no build cost, and wood does not spoil like grain. The
**storehouse as a second building** waits for the thing that actually needs
walls: stored goods, arriving with crafts and trade. At that point the
granary's measured-demand model (filled, then nearly drained → build)
transfers verbatim.

---

## Tools are not modeled

Basic stone/flint/wood tools stay priced into the yield table as universals
(the 2026-08-25 roster decision). The temptation to model them so that
later metal tools have something to replace is resolved the other way, as
archery already showed: model the *new* thing as equipment — made, decaying,
carried — and let its effect be the margin over the universal baseline.
Modeling stone tools now would mean building and tuning an equipment layer
whose net effect must be zero. The hafts-and-handles wood cost is folded
into the firewood draw and forgotten.

---

## Where this leads

Wood becomes the second column in the ledger beside food — the first need
that is not calories — which is what makes trade mean something. The rough
technology road beyond it, sketched 2026-09-01: pottery (need-driven off
the storage signals that already exist; also the pyrotechnic gateway to
smelting) → irrigation and the plough (the farming × husbandry synergy) →
sailing and trade as a system (sailing's unique job: the first mechanism by
which knowledge crosses oceans) → copper (Chalcolithic: native metal, then
kiln smelting) → bronze (the alloying step, gated on tin reaching you via
trade — bronze without trade degrades into "lucky tiles get better tools").
Metal tools, when they come, follow the bow pattern.

---

## Built and measured (2026-09-14, the first increment)

What is in code (population.h `advance`, constants beside the granary
block): the heat need in kg of wood-equivalent per person per day
(`fuelNeedKg`: ~1 kg to cook anywhere plus 0.12 kg per degree below 15°C —
about a tonne a year temperate, two to three subarctic, which is what the
ethnographic record measures); wood cover `sWood` as its own per-cell
suitability (buildMat credits bare rock, and stone does not burn); the
woodpile `fuelS` capped at 500 kg a head with no build cost; dung as the
second mode (4 kg per people-fed unit of herd); the byproduct (1.5 kg per
person-day times wood cover — ordinary rounds cover the cooking fire
wherever there are woods at all); and the ledger itself — the day holds
`P·wh/12` man-days, food's claim is the harvest actually eaten or banked
(a full larder frees hands), dedicated cutters come out of what is left,
and famine pre-empts the woods through the same hoarding gate that stops
granary work. A cold hearth kills like famine, weak first, quadratic in
the unmet share (`COLD_MAX` 0.02/day), and those deaths land in
`starvedYr`, so the burial signal that drives emigration now hears the
cold too. A draining pile is a wake-up deadline like a draining larder.

Measured over 500 years on seed 7 against the same run with hearths cold
(`test_resources.cpp`): the world is unchanged in the large — 7,625
settlements and 1.95M people against 7,397 and 1.93M, within run
divergence — and the labour gradient is the designed one: wooded
settlements spend ~0.5% of the day on fuel (the byproduct nearly covers
them), sparse country ~5.5%, treeless country ~12.6%. The map moved
exactly where it should: treeless-country population fell from ~22,000 to
~5,900 — cold steppe and tundra are hard to winter on with no trees and
no herds, and with husbandry barely invented by year 500 the dung mode has
no herds yet to run on. Cold kills ~400/yr worldwide by year 500 (4% of
hunger's toll), concentrated in ~50 settlements. When husbandry spreads,
the treeless cold should reopen — that is the dung mode's claim, still to
be observed.

## Deferred from the first increment

- **The wood stock as a depletable quantity.** Fuel gathering cannot
  meaningfully deplete standing forest at these populations (the increment
  of a few km² of woods outruns any village's hearths), so the stock state
  waits for its real driver — farming clearance — rather than being a
  gauge that never moves.
- **Farming clearance** (the terrain-deviation work), scarce lithics with
  trade, the storehouse, peat and charcoal, shelter reducing the need —
  all as designed above, none started.
- **Granary and bow work still draw on `buildMat`**, not the wood
  economy; unifying them belongs with the stock, when it exists.
- **Movers ignore heat**: a band judges ground by food and water alone,
  so colonists can settle ground they cannot heat and bleed until they
  leave. Emergently self-correcting but wasteful; site choice should
  eventually price the hearth.

---

## Open Questions

- The allocator's exact form: a strict priority stack, or marginal-utility
  weights? The stack is simpler and matches the existing hoarding gate.
- The wood stock's unit and radius: the settlement's woodshed against the
  regional pool pattern — wood is local, but how local?
- Byproduct fractions: what share of foraging labour yields wood/stone, and
  is the yield ratio fixed or cover-dependent?
- Does heat need interact with the winter hunting floor (time spent hauling
  wood is time not hunting)? The labour cap answers this automatically if
  both draw from the same budget — verify it does.
- When shelter later reduces heat need, does the hut become a building, or
  stay a universal? Deferred with huts themselves.
