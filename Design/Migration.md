# Migration

**Status:** Implemented — see [[Meta/Status Vocabulary]]

Since 2026-08-31 a settlement holds a claim rather than a fixed catchment, and hunger widens that claim before it sends anyone away: see [[Design/Borders]]. Emigration is now what a hemmed-in people do.

How people leave, move, and settle anew. The central idea: a migrating band is a settlement with velocity, and "settled" is an outcome, not a type. This note also introduces **storage** (the food a group has already harvested, as distinct from what the land offers) and the project's first **agent** — the band.

---

## Groups

A group has position, people P, knowledge, technology state, and a food store S. A settlement is a group whose staying put keeps working; a band is a group on the move. Same entity, same rules.

- Where K is high, staying wins and the group is a settlement.
- Where K is thin, the same rules produce perpetual motion. Since land condition R regenerates over ~33 years, a viable wandering path must loop through territory on roughly that period — seasonal circuits, transhumance. Nomadism is emergent, not authored. (Pastoral nomadism proper — herding raising steppe yield for mobile groups — is a future second technology in [[Design/Technology]].)

---

## Storage

The land offers a *flow* (the sustained yield K·R around the group's position); the group holds a *stock* S of harvested food.

- **Harvest** H: limited by both the land's flow and the group's gathering time. A settled group forages on full time; a moving band can spend only about a third of its day gathering, so its harvest tops out lower on the same terrain.
- **Consumption**: P people eat P rations a day. Surplus fills the store; deficit drains it.
- **Caps**: a settlement stores up to ~90 days × P; a band carries ~10 days × P.
- **Famine**: not a hard breakpoint at S = 0 — food is not shared equally, and as stores run low people hoard, so the bottom of the group loses access before the stock is gone. Deaths = starvation rate (up to ~2%/day) × *excluded share* × *harvest shortfall*: the excluded share ramps from 0 to 1 as the store's fill fraction drops below ~¼ of the cap, and the shortfall is the fraction of daily need the current harvest fails to cover. Both factors are needed — a group living hand-to-mouth on good land is poor, not starving — and at an empty store the rule reduces to the full deficit-driven rate. Fill *fraction* (not absolute days) sets the onset, so a settlement with a 90-day cap feels famine below ~22 days of food, a band below ~2.5. One famine rule for everyone; settlements and bands differ only in circumstances.

S evolves piecewise-linearly between events, so "when does the store hit zero (or full)" is computed and scheduled, never polled — a client of [[Design/Event-Driven]].

Storage is also the substrate for deferred mechanics: raiding steals stores, granary technology raises the cap, trade moves stores between groups.

**Calibration note**: storage rewrites the decline side of [[Design/Population]]'s dynamics. The verified behaviour — ~18% overshoot, settling at the stated capacity — is the spec to re-hit offline before the change lands.

---

## Knowledge

A group knows its surroundings out to a range, with accuracy decaying over distance — near things resolve exactly, distant things are rumours. No per-group map rasters: knowledge is a radius plus distance-scaled noise applied when candidate sites are evaluated.

## Moving as a whole: the default answer

Decided 2026-08-26, after the historians: for most of human history relocating the whole community was the normal thing to do and staying put was the exception. Foragers moved camp seasonally; early farmers practised shifting cultivation and moved the village every ten to thirty years as soils and firewood ran out; pastoralists move by definition; and whole towns were abandoned outright when the land failed. What makes a settlement genuinely fixed is **sunk investment** -- cleared fields, granaries, permanent houses. Sedentism is a consequence of things you cannot carry.

So when sustained scarcity forces a decision, the group first asks whether it can **all** go somewhere better:

- **Split when the problem is numbers; leave when the problem is the place.** Moving is the default because people are kin and a failing place fails for everyone; fission is the fallback for a world with no room left for the whole group. The colonization wave therefore appears only as the map fills -- exactly the historical order.
- **Judged on the rumour, not the truth.** The destination is valued at what the group has *heard* (`bestProspect`'s distance-noised estimate), because that is all they know. Arriving to a poorer valley than promised is a real outcome; the band re-evaluates on arrival like any other.
- **Sunk investment anchors the choice.** The destination must beat home by a factor of 1 + 0.25 per granary + 0.5 x farming expertise, so foragers and herders shift readily while a farming village with full granaries splits and keeps its fields.
- **Small groups get an exit.** Below the split minimum (50) a settlement could previously only sit and dwindle; relocation is now its only real answer to a failing site, gated at 20 people (fewer cannot survive a journey).
- **Scarce settlements wake in time to decide.** A settlement's ordinary horizon can be years long; without pulling the next wake to the decision moment, famine resolves the crisis mid-sleep -- starving down to fit rather than moving, with the question never asked. (Found in testing: a scarce settlement slept 1,383 days through its own 730-day deadline.)

**The trigger is growth held back by food** (2026-08-28). A group whose calorie needs are fully met grows at its maximum rate; growth saturates at phi = 1.11 (`PHI_CONTENT`, the same threshold that decides whether a technology is worth adopting). Anything short of that means food is what is limiting them, and that is reason enough to look for somewhere else -- long before the place is visibly failing. This replaced two narrower measures in turn: phi < 0.99, which only fired once a settlement was already past equilibrium, and a trailing-year count of starvation deaths. The burial count survives as a backstop for what an annual mean cannot see -- a sharply seasonal site reads comfortable on the year while the lean season still kills -- and panels show it as "Hunger: N lost this year".

Looking is not moving: the trigger is now true for most settled groups most of the time, and what decides is still whether a known place beats this one by the anchor factor. Because a survey of the horizon is the expensive part, a group that looks and finds nothing worth the move **backs off** -- doubling its patience up to ~32 years, and returning to the short cadence the moment real hunger arrives. In a 1,000-year world the sensitive trigger roughly doubled migration (4,272 moves, 854 splits, 1,150 settlements, 336k people) at about 2.5x the simulation cost.

**Ground passed on the way is judged against the goal** (fixed 2026-08-30). A band sets out for somewhere; land it crosses is an alternative to that plan, not to nothing. The old rule accepted anything within 10% *below* the target -- a margin pointing the wrong way -- so a band settled on the first passable cell, and since the target had been chosen on an inflated rumour, its own recently abandoned home cleared that bar easily. Groups with farming showed it most, because the yield multiplier makes their valuation of a site swing with their expertise while they walk.

The bar is now **hope that fades**: a passing site must beat the goal by 25% on the day they leave, parity comes within the year, and it keeps sinking toward half, so a band that has been walking for years will take distinctly less than it set out for rather than wander forever. One rule replaced three guards (an anchor on the abandoned site's value, a minimum on what could be targeted, and a re-settlement margin), all of which were treating the symptom.

Settling events record **how far the new home is from the old**, which is worth knowing and is also the instrument: this class of bug is visible as a distance near zero. Measured across ~19,000 relocations, median 324 km and 14 settling within 25 km (0.07%); under the old rule, median 242 km and four times as many close ones.

**One yardstick for staying and for settling** (fixed 2026-08-26): leaving is judged against the people you have (phi = capacity x condition / P), so settling had to be too. It was not -- founding tested a fixed 150-person threshold that ignored the band -- and a group that walked away from a valley because it could not feed three hundred would judge that same valley suitable the next day and re-found on it. Ground is now settled only if it can feed the band that would settle it *and*, for a whole community that picked up and left, only if it beats what they walked away from (`Band::leftCap`, measured with the same function that values every candidate). The old site fails that comparison by construction -- it is exactly equal to the bar -- so the outcome no longer depends on how far the band happened to travel between checks, and with it the old step-size dependence goes away (small time steps left a band standing on its own site when the "good ground underfoot" rule fired; large ones carried it 75 km clear).

Two rules that were tried and rejected on the evidence: barring an abandoned site outright (a patch over the inconsistency rather than a fix), and gating relocation on the land being measurably degraded (`R` is self-stabilising, so places almost never "fail" that way -- gating on it produced zero moves in a millennium). What actually drives migration here is that somewhere else looks better, which is a fair model of the real thing.

**A departing settlement takes its people with it** (fixed 2026-08-30). The record is not erased at the moment of departure -- it is marked `leaving` and swept when the step's events are done -- and for that window it must behave like an empty place. It did not: departure zeroed the headcount but left the cohorts standing, and `advance` recomputes the headcount from the cohorts, so the first wake after departure resurrected the settlement at full strength, complete with its hunger and its knowledge. It could then be picked as the world's inventor of farming, and was: the news read *"the Jiawiarar invented farming"*, the record was swept minutes later, and the real Jiawiarar -- the same people, the same id, walking or newly settled elsewhere -- had never taken it up. That is why farming appeared to be invented and then not practised. Departure now clears the cohorts, the catch-up pass skips settlements that are leaving, and the invention and adoption clocks ignore them.

The site they leave is **erased, not tombstoned** -- settlements carry a stable id, and panels resolve it each frame, so the settlement list never accumulates ghosts. Two traces remain: the land keeps the condition it was left in and recovers on the usual timescale (so an exhausted valley is a bad place to move to for a generation, and `moverCap` prices it accordingly), and a place that was invested in -- granaries, or sixty years of occupation -- leaves **ruins**: a small grey mark at close zoom, named by the tooltip, weathering away after four centuries. A forager camp of thirty that stood a decade leaves nothing, which is also archaeologically true.

In a 1,000-year test world this produces roughly four moves for every split, with most of the churn in the first two centuries before the map settles.

**Skills decide worth** (2026-08-26): every candidate site is valued at what *this mover* could make of it (`sim::moverCap`) — forager yield plus the farming bonus scaled by carried farming expertise and the herd-capacity bonus for practising herders — and the same measure gates actual founding. A herding band therefore takes steppe a forager walks past (~67,000 cells per test world are herder-only), and pastoral colonization of the grasslands continues after forager expansion has saturated — bands that find "nowhere to go" as foragers find pasture as herders. Arriving herders seed their settlement with stock driven along the march (0.25 per person × expertise), not the bare wild-capture seed. Band-owned herds en route remain deferred; the driven-stock seeding approximates them at the endpoints.

The radius is dynamic (capped at 600 km):

- **Base**: 150 km — what any group sees from where it stands.
- **Settled age**: grows toward +300 km with a ~30-year saturation — people wander their surroundings, and the marginal new ground per year shrinks. A fresh colony is near-sighted; an ancient settlement sees ~450 km.
- **Resting bands** scout: toward +100 km on a ~45-day timescale. Movement resets it — the stop-and-replenish rhythm doubles as reconnaissance.
- **Vantage**: prominence above the regional mean elevation adds the real horizon distance, 3.57·√(metres) km — a site 500 m above its surroundings gains ~80 km. A band deliberately climbing for vantage is deferred to its future decision step.

(The earlier idea that scarcity grows the range is superseded by settled age; hungry settlements act by splitting, not by seeing farther.)

---

## Splitting and the band's journey

When a settlement's food per head φ stays below 0.99 (just under the equilibrium point — the overshoot decline never dips much deeper) for two years and P ≥ ~50, about a third of the people leave as a band, inheriting the settlement's current knowledge and technology (aware/practising state and expertise travel — demic diffusion, historically the dominant way farming spread, and the mechanism that carries technology across gaps wider than the contact radius in [[Design/Technology]]).

The band:

- commits to a *direction* (the most promising known or rumoured prospect), not a coordinate;
- moves ~15 km/day, foraging a corridor on its reduced time budget;
- re-evaluates as it travels — nearby ground resolves accurately, so finding a better spot en route and discovering the rumoured valley is worse than hoped both come from the same mechanism;
- stops to replenish its store when the terrain allows and moves on while pressure remains — punctuated, staged migration;
- **settles** where unclaimed ground's known value beats continuing (respecting the ~80 km spacing), founding a new settlement — this closes the open question of static placement at world creation;
- **merges** into any settlement it reaches if it falls below ~20 people.

**Range is emergent**: a band can cross barren ground only as far as its stores carry it — ~10 days × 15 km/day ≈ 150–200 km, arriving full. Deserts are barriers with a measurable width until technology changes the numbers (camels, boats).

---

## The first agent

The band is the first instance of Delegate's Agents concept ([[Design/Core Concepts]]): an entity with position, people, knowledge, and a goal, whose one decision is which way to go. It is deliberately minimal — arrived at from need, like the aware state was for technology staging.

**Deferred — the settlement agent**: the original framing (a problem-solving actor spawned by overpopulation) is deferred until settlements have more than one solution to choose between. Its future repertoire: raiding neighbours for their stores, sending expeditions to scout beyond the knowledge range, and choosing between them. Until then, splitting is a decision rule at the settlement's existing wake-ups, not an agent.

**Deferred — band fission**: bands splitting over competing choices (stay longer vs move, or diverging directions).

---

## Open Questions

- ~~Bands currently cross water as if it were maximally barren land~~ Superseded by passability: unfrozen open water is crossed at half speed (the raft stand-in made explicit), **frozen** water — seasonal local temperature below −2 °C — is walked at full speed, and a major unfrozen river slows a band to fording pace. Stores still drain on water and ice (no forage), so range limits remain; winter is the crossing season, and ice-bridge migrations over northern straits are possible. Boats stay a future technology.

- Exact harvest time-budget numbers. (Resolved: the settled cap of 90 days predates granaries — pits and baskets — and built granaries extend it; see [[Design/Technology]].)
- How knowledge ages: do rumours of a valley persist after the band that heard them settles?
- What a group passing a foreign settlement does short of raiding (trade, tension, absorption?).
- Does a departing band leave a scar — reduced P is stored, but should R take a parting hit?
