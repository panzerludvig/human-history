# Culture

**Status:** Implemented — see [[Meta/Status Vocabulary]]

Who people are, what they call themselves, and what they have become good at. Decided 2026-08-29.

---

## Names follow communities, not places

Every settlement the world opens with is its own **culture**, with its own name and its own way of speaking. Cultures spread only by colonisation, so after a few centuries they map the lineages of expansion — a culture is a family of settlements descended from one origin.

The naming rule turns on a distinction the migration system made important: whole-settlement relocation is now the common case (~4,300 a millennium against ~1,000 splits), so most bands are not colonists at all.

| Who | Called | On settling |
|---|---|---|
| A settlement that picked up and moved | *the Miarota, on the move* | keeps its name — same people, new site |
| Colonists split off from a parent | *Miarota colonists* | name their own new home |
| A raiding party | *Miarota raiders* | dissolves back into home |

A name therefore tracks a community through space, and ruins keep theirs: the tooltip reads *"Ruins of Poetti, abandoned year 812"*.

## How they sound

A culture owns a small sound inventory drawn at world start — five onsets, four vowels, three codas and two endings, from shared pools. Every name in that culture is one or two syllables from its own sounds plus one of its endings, so relatives sound related without anything coordinating them. From a real run: *Skaskaror of the Draor*, *Kiana of the Kiadrelni*, *Thiaos of the Thoulen*, *Ditiia of the Veitiaia*. Overlap between distant cultures is expected and harmless at this scale.

## Affinities: what you do is what you become

Each settlement carries five **affinities** — hunting, gathering, farming, herding, fighting — that drift toward what its people actually live on, over about a century, and feed back as a small bonus (up to +15%) to that same activity.

- The four food affinities are fed by the *share* of the settlement's food coming from each source, all of which the flow calculation already produces.
- **Fighting is not fed by food.** It is learned by raiding *and by being raided* — both sides gain from every fight — and fades over ~200 years of peace. So a people who are robbed repeatedly become dangerous rather than merely poor, and a long-quiet people go soft.

Affinities travel: colonists, movers and raiders all carry their parent's, so what a people is good at spreads with the people. That is the mechanism culture-level traits will be built on.

The discipline here is that this is a **positive feedback loop** — do more hunting, get better at hunting, do more hunting — and this project has been bitten by those before. The gain is deliberately small and the timescale slow, so it accumulates into identity rather than optimisation. Two settlements on identical land diverge because of what they happened to start doing, which is the point.

**Measured consequence to watch**: because every settlement specialises in *something*, the bonus is close to a global lift rather than a redistribution — a test world grew from 374k to 660k people. If that reads as too generous, the fix is to centre the bonus (no change at average affinity, penalty below it) rather than to shrink it.

## Deferred

- **Culture drift and new cultures forming** — the structure is in place (cultures are first-class, settlements reference one), so this is additive.
- **Culture-level traits**, aggregating settlement affinities upward, and what that does to who splits from whom.
- Any relationship *between* cultures: kinship, hostility, alliance, or a settlement changing the culture it belongs to.
