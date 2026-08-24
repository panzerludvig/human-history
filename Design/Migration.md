# Migration

**Status:** Implemented — see [[Meta/Status Vocabulary]]

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

A group knows its surroundings out to a range, with accuracy decaying over distance — near things resolve exactly, distant things are rumours. Under sustained scarcity the range grows (hungry people explore more). No per-group map rasters: knowledge is a radius plus distance-scaled noise applied when candidate sites are evaluated.

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

- Bands currently cross water as if it were maximally barren land (K = 0, stores drain): straits within store range are passable, oceans are lethal. A deliberate stand-in for rafts and fords until boats are a technology — revisit then.

- Exact harvest time-budget numbers, and whether the settled cap (90 days) should predate granary technology at all.
- How knowledge ages: do rumours of a valley persist after the band that heard them settles?
- What a group passing a foreign settlement does short of raiding (trade, tension, absorption?).
- Does a departing band leave a scar — reduced P is stored, but should R take a parting hit?
