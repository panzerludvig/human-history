# 23 — Retune farm land per person against early farming

**Status:** idea (2026-09-28) — needs the literature checked and the numbers decided

## Problem

The land a farming people needs and can work looks several times too large for early
farming. The model feeds one person from 4 ha of tilled land and lets one person tend 8 ha.
Estimates for early farming commonly put the land actually cropped in a year at well under
a hectare a person (perhaps 0.3 to 1 ha, depending on yields and on whether it was intensive
gardening, as argued for the early European Neolithic, or shifting cultivation). 4 ha a head
is defensible only as the whole of a long-fallow cycle, which the design says it includes;
8 ha tendable a person is high even so. The numbers decide how big a farming village's
clearing is, how many farmsteads it needs, and how much land reverts under order 12.

Also: the village's daily-fields reach (4.3 km, from the walking curve) is supported by site
catchment studies (Chisholm: most farmland within about 1 km, rarely worked from home
beyond 3-4 km; Vita-Finzi and Higgs: a farming catchment of about an hour's walk). The reach
itself is order 24's subject; this order is the land per person.

## Evidence

- `src/settlement.h:351` `TILLED_YIELD_PKM2 = 25` (people fed per tilled km2 at full
  suitability and expertise: 4 ha a head) and `settlement.h:354`
  `FARM_KM2_PER_PERSON = 0.08` (8 ha a person tendable).
- [[Design/Technology]] §Farming: "4 ha a head at full skill on prime ground, with the long
  fallow priced in"; "8 ha a person, `FARM_KM2_PER_PERSON`".
- The discussion of 2026-09-28 that raised it (village fields of 58 km2 questioned).

## Design

_To be filled in._ The questions:

- **What "tilled" means on the map and in the model**: the land under crop this year, or
  the whole rotation with its fallow? The two differ several times over, and the numbers
  follow from the choice.
- **Yield and land per head** from the literature for the farming the model represents,
  with sources written into the design note.
- **What a person can work**, and whether it should be per person at all or per worker
  (the model counts children and elders in `P`).
- Whether the numbers vary with the ground (shifting cultivation in forest against
  permanent fields on alluvium) or stay one figure for now.

## Outcome

_To be filled in._

## Files

_To be filled in._ Likely `src/settlement.h`, `Design/Technology.md`, and the probe runs
that show the effect (farming settlements, tilled area, farmsteads).

## Done when

_To be filled in._

## Depends on

Nothing. Orders 12 and 13 use these numbers; retuning after them changes what they
measure, not how they work.
