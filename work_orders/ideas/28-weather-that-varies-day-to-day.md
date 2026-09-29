# 28 — Weather that varies from day to day

**Status:** idea (2026-09-29) — the design is open

## Problem

The daily temperature is a fixed cosine: every day at a place is the same curve, peaking at
15:00 local solar time, with a range set only by the season and by distance from the sea.
Real temperature has roughly that average but varies a great deal around it, and the
cosine makes the world look rigid and forced wherever the temperature is shown.

What real days do that the cosine does not:

- **Clouds and humidity set the range.** A clear, dry night loses heat fast and the range
  is wide; an overcast or humid day and night stay close together.
- **Weather moves the whole day.** Warm and cold spells last several days; a cold front
  can make the temperature fall all afternoon, overriding the daily cycle.
- **The shape is not symmetric.** Temperature climbs quickly after sunrise and decays
  slowly through the night; the peak time and the range change with day length, and at
  the poles in polar night or midnight sun the cycle almost vanishes.
- **Snow, wet ground and wind** shrink the range; dry ground and calm air widen it.

`Technical/Globe Viewer.md` also records that weather variability, such as drought years,
affects nothing yet.

## Evidence

- `src/atmosphere.h:780-786`: `DIURNAL_PEAK_HOUR = 15` and `diurnalPhase`, a cosine.
- `src/atmosphere.h:776-777`: `PRE_DIURNAL_LAND = 5.0` K half-swing deep inland,
  `PRE_DIURNAL_SEA = 0.5`; `surfaceT` scales the land's by continentality (`:934-935`).
- `src/inspect.h:76-90`: the tooltip's current temperature is the seasonal mean plus half
  the stored daily range times `diurnalPhase` at the cursor's local solar time.
- `Design/Weather.md`: temperature is prescribed; "the diurnal cycle is applied
  analytically from the sun and the stored swing amplitude".
- As far as the code shows, the population model uses seasonal temperatures only, not the
  hour; the daily cycle feeds the climate build and the tooltip.

## Design

_To be filled in._ Open questions:

- **Where the variation comes from.** A seeded stochastic stand-in (spells of a few days,
  correlated over a region, and a cloudiness that scales the daily range) or weather from
  a model: the quasi-geostrophic weather on the geodesic mesh (`src/qg2geo.h`, sweep build
  only) is the atmosphere being redone there. A stand-in is a constant for what the model
  does not model and says so (`standards/general.md`).
- **Whether the simulation feels it.** If only the tooltip and the picture show it, it
  is presentation. If cold spells can freeze water early, kill in a hard winter, or
  drought years can fail harvests, it is a game mechanic, and the population model needs
  a daily or weekly temperature and rain, not only seasonal means.
- **What stays deterministic.** Same seed and same date, same weather
  (`standards/general.md` §Verification), including after a save and load.
- **What the picture shows.** Clouds already drift with the stored wind; whether the
  cloud field and the temperature's variation should be the same weather.
- The note it goes in: a new section of `Design/Weather.md`, moving it off Implemented.

## Outcome

_To be filled in._

## Files

_To be filled in._ Likely `src/atmosphere.h`, `src/inspect.h`, `shaders/globe.frag` if the
picture shows it, and the population model if the simulation feels it.

## Done when

_To be filled in._

## Depends on

_To be filled in._ Possibly the atmosphere on the geodesic mesh, if the weather comes from
a model rather than a stand-in.
