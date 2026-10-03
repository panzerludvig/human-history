# 29 — Land below sea level is not sea: the Nile reaches the Mediterranean

**Status:** idea (2026-09-29) — split from the Nile findings; the floodplain half is idea 30

## Problem

On the Earth template the Nile does not reach Egypt. Its water runs down a course west of
the real valley from about 22 N, through the Western Desert, and ends in the Qattara
Depression, which lies below sea level and so is sea to the model. The valley from Luxor
to Cairo and the Delta carry only local runoff, about 1% of the river, and the Nile
valley, which history fills with people, stands nearly empty.

The cause is general: every cell at or below sea level is ocean, whether or not it
touches one. Any inland depression becomes a sea and captures whatever drains toward it.

## Evidence

Measured with a scratch probe on the Earth template (the game's `earth` world), `main` at
`22b1980`, reading `World::build`'s `hydro` per 20 km cell.

- **The Nile's course**, traced downstream from 16 N (drainage 6.18M km2 equivalent,
  `hydrology::Result::parent`): 18-22 N at 29.8-30.2 E (the real river is at 30.5-31.5
  E), 22.4-25 N at 28.2-30.0 E (real: 31-33 E, Aswan 24.1 N 32.9 E), then 25.6-28.7 N at
  27.2-28.7 E, reaching `h <= 0` at 28.92 N 26.98 E with 6.68M km2: the Qattara
  Depression. Along the real valley the largest drainage is 7k km2 at 26 N and 23-60k
  from 28 N to the Delta at 31 N.
- **Land below sea level is sea.** Cells with `heightM <= 0` at 29.1-30.3 N, 26.4-28.2 E
  (Qattara, -21 m at 29.5 N 27.5 E), and the Dead Sea at -549 m. The hydrology and the
  population model treat every `h <= 0` cell as ocean.
- **The river's size**, an aside: 6.68M km2 at the reference runoff of 300 mm a year
  (`hydrology::REF_RUNOFF_MM_YR`) is about 2000 km3 a year; the real Nile carries about
  84 at Aswan. The upper basin is far wetter than the real one, or the runoff rule
  overstates it.

## Design

_To be filled in._ Open questions:

- **What a depression below sea level is.** Land, when it does not touch the ocean: a dry
  basin, a salt pan or an endorheic lake (the Dead Sea, the Caspian), filled and drained
  by the hydrology like any other basin. Which hold water follows the climate's water
  balance, as lakes already do. What is drawn there, and what the population model makes
  of a salt pan.
- **Where the river runs.** Whether the template's rivers are also steered by a real
  river dataset (Natural Earth, as its lakes already are) where the 20 km grid loses a
  narrow valley, or whether fixing the depressions is enough for the Nile to find its
  own valley.
- **Other worlds.** Random worlds can have depressions too; the rule is general, not an
  Earth patch.
- **The discharge.** Whether the river's size is in scope here or goes to the climate.

## Outcome

_To be filled in._

## Files

_To be filled in._ Likely `src/hydrology.h`, `tools/make_earth*.py` and `data/earth.bin`
(the template), `shaders/globe.frag` if basins are drawn differently, `Design/Terrain.md`.

## Done when

_To be filled in._ The natural check: traced downstream from Sudan, the Nile reaches the
Mediterranean through the Delta, and its drainage along the real valley is within a few
percent of its drainage at the Sudanese border.

## Depends on

_To be filled in._
