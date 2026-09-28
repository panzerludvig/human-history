# 24 — One travel cost: how hard it is to cross ground, by terrain and means

**Status:** idea (2026-09-28) — the design is open

## Problem

How far people can go is written as fixed speeds, separately in each system that needs it,
and none of them looks at the ground or at what people have to carry with:

- **Farming reach**: fields lose value with the round-trip walk at a fixed 4.8 km/h over a
  12-hour day (`farmCommute`). A village on open flat ground with pack animals works the same
  distance as one in broken hills with nothing but its back.
- **Bands** move at a fixed 15 km a day, scaled only by daylight, water (rafts at half
  speed, ice at full) and major rivers (fording pace).
- **Raids** are journeys, and distance is their cost, at the same fixed pace.
- **Awareness** (order 22) would naturally follow from how far people travel.

The wish (2026-09-28): a base walking speed, modified by the terrain crossed and by the
means people have (pack animals once herding exists, later carts and boats), used by every
system that needs a distance to cost something.

## Evidence

- `src/settlement.h:313-317` — `FARM_WALK_KMH = 4.8`, `FARM_DAY_H = 12`, `farmCommute`.
- `src/settlement.h:216` — `BAND_SPEED_KM_DAY = 15`.
- `src/bands.h:21-22, 464-476` — `RAFT_FACTOR`, `RIVER_CROSS_FACTOR` and the band's daily
  distance.
- `src/raids.h` — raid journeys; [[Design/Conflict]] "It travels".
- [[Design/Migration]] — passability: open water, ice, rivers.

## Design

_To be filled in._ The questions:

- **What the terrain contributes**: slope (from the terrain function at full resolution),
  cover (forest and marsh slow, grass and steppe do not), rivers and water (the existing
  passability rules folded in), altitude?
- **What the means contribute**: pack animals from husbandry (carrying more, not walking
  faster, for fields and moves), and later technologies (carts, boats) as they come.
- **Cost of what**: a single cost per distance crossed, in hours, that farming reach, band
  movement, raids and awareness all read; or separate uses of one terrain model.
- **The farming reach** (moved here from order 17, 2026-09-28). The village's daily fields
  are capped at `VILLAGE_FIELDS_KM2 = 58` (`src/settlement.h:345`), worked out by hand from
  a walking curve that no code calls: `farmCommute`, `FARM_WALK_KMH`, `FARM_DAY_H`
  (`settlement.h:313-316`) and `FIELD_WORTH = 0.85` (`settlement.h:344`, where daily fields
  end: the walk costing 15% of the day, "where the ethnography puts the village-field
  edge"). The farmstead spiral's outer limit is derived the same way, in a comment only.
  No field's value depends on its distance today; the cap is the whole effect. With travel
  cost, the reach, and so the cap, follows from the terrain and the means, and is
  computed. Open: whether fields also lose value with distance (near land farmed first,
  far land yielding less for its commute), and whether `FIELD_WORTH` stays as what stands
  in for the household's choice between walking and moving out to a farmstead.
- **Territory in travel time** (decided 2026-09-28): claims are measured in travel time,
  not kilometres, so the claim floor and cap (`CLAIM_FLOOR_KM` 20, `CLAIM_CAP_KM`,
  `src/settlement.h:52` and on) follow from the terrain and the means. Open flat country
  with pack animals gives larger territories than broken hills. The spacing between
  settlements then follows with no rule of its own: a new settlement needs room for a claim
  (`claimFits`, `src/claims.h:85`), and claims do not overlap. Orders 17 (the shortest
  move) and 21 (opening spacing) read the same travel time.
- **Resolution and speed**: the fields' reach is kilometres, a band's day is 15 km, the
  grid is 20 km; how finely the cost is sampled, and whether it is cached per cell.

## Outcome

_To be filled in._

## Files

_To be filled in._

## Done when

_To be filled in._

## Depends on

Nothing. Orders 22 (awareness) and 23 (farm land) may want to wait for it, or be revisited
after it.
