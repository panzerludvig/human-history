# Technology

**Status:** Concept — see [[Meta/Status Vocabulary]]

How settlements learn to do new things. The core ideas are inherited from Delegate's Tech Tree ([[Meta/Inherited from Delegate]]) — this is the first Delegate design concept re-examined and adopted. The first technology is farming.

---

## Principles (kept from Delegate)

- **Dark** — there is no visible tree. Nobody, player included, knows what could be discovered until it is.
- **Dynamic** — which technologies exist and when they appear varies between worlds.
- **Shared** — a discovery spreads between settlements; it is not owned.
- **Automatic** — discovery is never a menu choice. Settlements discover technologies themselves, driven by their circumstances. There is currently no player at all, so this is not just a principle but a necessity.

---

## Discovery: pressure invents

The trigger for discovery is **need**. [[Design/Population]] already produces the historically correct signal: in the overshoot phase, food per head φ = K·R/P falls below 1 and the settlement is in hardship. The discovery rate for farming scales with that scarcity (and with population — more people, more experiments). Comfortable foragers don't invent agriculture; hungry ones do.

This makes the famine mechanism the *cause* of agriculture with no new machinery: overshoot → hardship → farming → capacity rises → the next growth wave. Different settlements cross the threshold at different times, so agriculture appears independently in a few places and spreads from there — as it did on Earth.

Mechanically, discovery is an event, not a poll: sample the discovery moment from the current rate (an exponential draw) and schedule it; resample only when the settlement's pressure changes at its existing wake-ups. This is a client of [[Design/Event-Driven]].

---

## Spread: proximity

A settlement near one that already farms adopts the technology far faster than it would discover it independently — contact, not broadcast. Technologies therefore have geography: fronts that radiate outward from their points of origin across the map ([[Design/Map-Centric]]).

---

## Expertise

Knowing a technology is separate from being good at it (kept from Delegate). Each settlement holds an expertise level 0–1 per technology, growing over years of practice, which scales the technology's effect. A settlement that just learned farming gains little; three generations later it is transformed. A technology that spreads by contact arrives at low expertise — which answers Delegate's open question about how spread and expertise interact.

---

## Farming (the first technology)

Raises food yield where conditions allow — enough warmth and moisture, gentle slope, workable substrate — with the multiplier scaled by expertise. Grassland and river valleys become the prizes; tundra stays foraging country. Irrigation will later multiply water use, so rivers start deciding where the large settlements are, through the same min() in [[Design/Population]].

Represented as an entry in a technology table, not a hardcoded flag, so herding and fishing slot in beside it.

---

## Future additions (deferred from Delegate, not discarded)

These were part of Delegate's Tech Tree design. Each is deferred because its prerequisite doesn't exist yet, not because it was rejected:

- **Investment** — the player lever: accelerating a technology's development by directing resources at it. Deferred until there is a player (or any deciding agent) to pull the lever. When guidance/autonomy arrives, this is the natural first point of contact between the player and technology.
- **Stages (theoretical → developed → improved)** — a technology's life as a state machine. For now the expertise scalar covers the same ground with one number; the staged model becomes worth its weight when technologies gain distinct practical applications that unlock partway (e.g. a theoretical technology that can be invested in before it works). Revisit together with investment.
- **Soft probability graph** — developing one technology shifts the discovery probability of related ones, giving a fuzzy graph rather than a fixed tree (and enabling Delegate's focus-area and catch-up-via-conflict effects). Meaningful once there are roughly ten technologies; with one it is pure overhead. The technology-table representation keeps the door open.

---

## Open Questions

- What exactly sets the discovery rate — scarcity alone, or scarcity × population, and with what constant so first farming appears at a plausible world age?
- Contact radius and adoption time for spread; does spread require sustained proximity or a single encounter?
- How fast does expertise grow, and does it decay if a technology goes unused?
- Does farming change the map — cleared fields as terrain deviations ([[Design/Terrain]])?
