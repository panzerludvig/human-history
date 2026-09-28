# 22 — The awareness range follows from the model, not from set distances

**Status:** idea (2026-09-28) — split out of order 17; what awareness follows from is open

## Problem

How far a settlement or a band knows its surroundings decides where people move to, and
since order 17 also who is in contact with whom: whom they learn from, whom they raid,
where a failing band finds refuge. Yet the range is built from set distances:
`AWARE_BASE_KM = 150`, growing by `AWARE_GROWTH_KM = 300` over `AWARE_TAU_DAYS` (30 years)
for settlements, by `AWARE_REST_KM = 100` over 45 days for resting bands, capped at
`AWARE_CAP_KM = 600`. Only the vantage term, the horizon distance from the site's
prominence (`3.57 * sqrt(metres)` km), follows from anything.

Violates `standards/general.md` §Thresholds and clocks (a constant only for what is not
modelled; what follows from the model is computed from it).

## Evidence

- `src/settlement.h:221-240` — the constants, `vantageKm`, `settlementAwareKm`,
  `bandAwareKm`.
- [[Design/Migration]] — "The radius is dynamic": base 150 km, settled age toward +300 km
  over ~30 years, resting bands toward +100 km over ~45 days, vantage, cap 600 km.
- Uses: the move search (`bands.h`, `bestProspect`), the awareness zones drawn round
  selected settlements and bands (`main.cpp`, `uAware`), and after order 17 the contact
  network.

## Design

_To be filled in._ What the range could follow from, all things the model has:

- **Walking.** Bands move `BAND_SPEED_KM_DAY` (15 km a day); people know what they, or
  those they meet, have walked. A range from time spent roaming at the model's own speed,
  rather than a set base.
- **People.** More people roam more ground and hear more news; a range growing with
  population, or with person-years spent at the site.
- **Age.** Knowledge accumulates while a place is lived in: the settled-age growth, but
  from how much ground its people have covered rather than a set 300 km and 30 years.
- **What stops it.** The cap: from the ground (sea, mountains, desert limit how far
  anyone roams), from a journey's length (how far one can go and come back in a season),
  or nothing.
- **Vantage** already follows from the terrain and stays.

The calibration is the other half: order 17 makes contact equal awareness, so whatever
this changes also changes how fast knowledge spreads.

## Outcome

_To be filled in._

## Files

_To be filled in._ Likely `src/settlement.h`, `src/bands.h`, `Design/Migration.md`.

## Done when

_To be filled in._

## Depends on

17, if the contact network's behaviour is to be measured with awareness as it is first.
