# 30 — Floodplains: desert rivers that feed people

**Status:** idea (2026-09-29) — split from the Nile findings; the routing half is idea 29

## Problem

A river through a desert adds almost nothing to the land beside it but fish. A cell's
cover, and with it its forage, its farm suitability and its pasture, comes from the
climate's moisture alone. The river adds drinking water and fish; the ground along it
is the same as the desert beside it. There is no floodplain: no strip of riverside woods
and reeds, no ground the flood waters and renews, no farming on it. The Nile, the
Euphrates and the Indus, the rivers that made the first dense populations, are barely
more habitable than the deserts they cross.

The desert itself is also too kind: Egypt's moisture makes steppe and shrub where the
real country is hyper-arid, so the contrast that made the valley is missing from both
sides.

## Evidence

Measured with a scratch probe on the Earth template, `main` at `22b1980`, reading
`World::build`'s `pop` fields per 20 km cell.

- **Capacity along the river against 6 cells east of it**, K people per cell. Nile
  16-31 N: river 151-244, beside 127-223; the river's own addition is fish (`kFish` 63-138
  on the river, 0.2-5 beside), forage is the same on both (`kFoodP` 130-225).
  Euphrates/Tigris 31-34 N: river 166-175, beside 136-150. Indus 25-30 N: river 200-474.
  The Danube at 44-45 N, a river in a wet climate: 656-676.
- **Why the river adds no forage.** `population::build` (`src/population.h:112-137`):
  cover comes from `terrain::mixtureAt` with the climate's moisture; the river enters
  only as `nearRiver`, which pulls the substrate toward alluvium (`src/terrain.h:406`)
  and changes no cover. `farmSuitability` and `pastureSuitability`
  (`src/population.h:65-74`) read cover only.
- **The desert.** Egypt's moisture is 0.16-0.20 along and beside the Nile: steppe and
  shrub, farm suitability 0.4-0.6 and pasture 0.6-0.8 away from the river.
- **The start.** No settlement of the world's 400 starts in 16-32 N, 29-35 E.
- `Design/Population.md`: "Farming will raise food yield and irrigation will multiply
  water use; then rivers start deciding where cities go." Irrigation is listed among the
  things not yet modelled; `Design/Resources.md` places it after the plough.

## Design

_To be filled in._ Open questions:

- **A floodplain.** Should a large river give the land beside it its own ground, whatever
  the climate: riverside woods and reeds, and a strip the flood renews? How wide (the
  Nile's valley is 10-20 km, about one cell), and how its size follows the river's
  discharge. Whether it is a cover of its own, mirrored in the shader, or a
  sub-cell share of the cell.
- **Farming on it.** Flood-recession farming needs no new technology, only farming: the
  flood waters and fertilises the field. Canals and basins are irrigation, a later
  technology. Which of the two this is, and whether floodplain farming is what teaches
  farming first (the need clock weights by farm suitability).
- **Foraging on it.** Before farming, riverside woods, reeds, waterfowl and fish made the
  late-Palaeolithic Nile one of the richer places to forage; how much of that is the
  floodplain's cover and how much the fish the model already has.
- **The desert's climate.** Whether Egypt's moisture is a calibration error of the rules
  climate (`Design/Weather.md`), fixed here, in its own order, or left.
- **What hospitable means.** A target to design against: the Nile valley's capacity next
  to good rain-fed farmland, and settled early on the Earth template.

## Outcome

_To be filled in._

## Files

_To be filled in._ Likely `src/terrain.h` and `shaders/globe.frag` (a floodplain's cover,
mirrored), `src/population.h` (capacity), `src/hydrology.h` (what a cell knows of its
river), `Design/Population.md`, `Design/Terrain.md`, `Design/Technology.md` if it touches
farming's discovery.

## Done when

_To be filled in._ A probe along the Nile, Euphrates, Indus and a wet-climate river,
printing drainage and capacity per cell against the desert beside it.

## Depends on

_To be filled in._ 29 for the Nile to be the test case: until its water reaches Egypt,
Egypt has no river to be a floodplain of. The mechanism itself does not need 29, and the
Euphrates and Indus show the same problem today.
