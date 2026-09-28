# 21 — A configurable number of starting settlements, at the best sites

**Status:** idea (2026-09-28) — split out of order 17; the questions below are open

## Problem

How a world opens is decided by two numbers nobody can set: `MAX_SETTLEMENTS = 400`
(`src/settlement.h:120`), a starting count written as a cap, and `MIN_SETTLEMENT_K = 150`
(`settlement.h:114`), a floor a site's capacity must clear. Between them and an 80 km
spacing literal, `population::build` picks the opening sites. The count should be a world
parameter the player chooses, like land share and concentration, and the sites simply the
best available.

Serves `standards/general.md` §Thresholds and clocks (a constant only for what is not
modelled; the floor is not needed if the count and the ranking decide).

## Evidence

- `src/population.h:203-236` (`population::build`): candidates are local maxima of K over a
  5x5 window with `K >= MIN_SETTLEMENT_K` (209), taken best first, at least 80 km apart
  (228, a literal), until `MAX_SETTLEMENTS` (`settlement.h:120`); each opens at half its
  sustained capacity, `P0 = K * SUSTAIN_R * 0.5` (232).
- `src/world.h:65-70` — the world's parameters: seed, earth, land percent, concentration.
- `src/menus.h` — the New World menu's fields for those parameters.
- `src/savefile.h` — the parameters are saved and the world rebuilt from them on load.

## Design

_To be filled in._ The questions to settle:

- **The parameter.** Its name, its range and its default (400 today); a field in the New
  World menu beside land and concentration; saved with the world.
- **Spacing** (decided 2026-09-28): opening settlements are spaced by the claims they will
  make, not by a distance: a site is taken only if there is room for its claim beside
  those already placed, as for any settlement founded in play (`claimFits`). The 80 km
  literal (`population.h:228`) goes. Once order 24 measures claims in travel time, the
  opening spacing follows the terrain with no change here.
- **When there are fewer good sites than the count asks for.** Open with fewer and say so,
  or take worse sites down to what can hold a group at all?
- **The opening group's size.** Half its site's sustained capacity today; kept?

## Outcome

_To be filled in._ The world opens with the chosen number of settlements at the best sites,
and neither `MAX_SETTLEMENTS` nor `MIN_SETTLEMENT_K` takes part in it.

## Files

_To be filled in._ Likely `src/population.h`, `src/settlement.h`, `src/world.h`,
`src/menus.h`, `src/main.cpp`, `src/savefile.h`, and the probes that build worlds.

## Done when

_To be filled in._

## Depends on

Nothing.
