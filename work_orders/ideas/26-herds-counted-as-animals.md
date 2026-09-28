# 26 — Herds counted as animals, with kinds of livestock

**Status:** idea (2026-09-28) — the follow-up to order 18's rename; the design is open

## Problem

A settlement's herd is a single number in "people it can feed", not animals. Order 18
renames it to a head count and states, for now, that one head feeds one person, so nothing
changes in play. The model should count animals as animals: a head of cattle and a sheep
feed, graze, breed and burn differently, and later the game wants several kinds of
domesticated animal, suited to different ground (reindeer on tundra, cattle on grass,
sheep and goats on rough ground, pigs in woodland).

## Evidence

The herd's unit is built into these, each of which must be restated per head (and later
per kind):

- `src/settlement.h:285` `HERD_PASTURE_K` — pasture capacity, in people-fed per km2.
- `src/settlement.h:284` `HERD_GROWTH_YR` — one growth rate for all livestock.
- `src/settlement.h:388` `DUNG_KG_PER_FED` — dung fuel per people-fed unit.
- `src/settlement.h:282-283` `LOOT_HERD_SHARE`, `FARMYARD_SHARE_POP` — raiding and the
  household animals.
- `src/technology.h:24, 296` `HERD_SEED` — the herd a people starts with.
- The herd's food share (the `0.85` in `population.h` `foodTerms`, `driftAffinity` and
  `bands.h` `moverCap`, named by order 09), the pasture cap (`population.h:677`), the herd
  step (`population.h:608-617`), raid value (`src/raids.h:64-66, 103`), bands carrying
  herds (`src/bands.h`), and the panels (`src/inspect.h`).
- [[Design/Technology]] §Animal husbandry — "one technology covers the household cow and
  the steppe flock"; herd in people-fed units.

## Design

_To be filled in._ The questions:

- **What one head is**: food per head per year (milk, meat, blood; the offtake a herd
  sustains without shrinking), pasture per head, dung per head, growth rate.
- **Kinds of livestock**: which kinds, what ground and climate suits each, whether each is
  its own technology or husbandry covers them and the ground picks, and how a people comes
  to keep a second kind.
- **How the knock-ons are restated**: pasture capacity in head per km2, dung per head,
  loot in head, the herd's share of the diet.
- **Calibration**: what the change must keep (herd-fed peoples as viable as today, and the
  dung arithmetic for the treeless cold) or deliberately move.

## Outcome

_To be filled in._

## Files

_To be filled in._

## Done when

_To be filled in._

## Depends on

18 (the head count and its name), 19 (husbandry from need).
