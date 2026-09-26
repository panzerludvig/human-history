# 09 — One name for each shared constant; thresholds derived from their tables

**Status:** open (2026-09-15) — runs alone, after the orders whose files it shares have merged

Line references re-checked against f9c731f on 2026-09-26; those into `src/main.cpp` moved to 5087977.

## Problem

Pi is spelled as a literal on about 105 lines in four spellings. Earth radius, days per year,
the 6.5 lapse rate, the row-cosine expression, the 0.85 herd factor and "never" as a time
each recur ten to twenty times with a named constant already present somewhere. Three
vector types exist. Several thresholds are literals compared against tables whose maximum
is computable, which is the recorded bug class from 2026-08-23.

Violates `standards/general.md` §Names carry units and §Thresholds and clocks, and
`standards/cpp.md` §Numbers.

## Evidence

- Pi: `3.14159265` about 85 sites, `3.14159265f` 29, and `3.14159265358979323846`,
  `3.14159` (106 lines across `src/` and `globe.frag`, 47 of them in `atmosphere.h` and 19
  in `sweep.cpp`); named at `src/qg2geo.h:43` (`PI`), `src/camera.h:32` (`camera::PI`,
  formerly `main.cpp:147`), `src/daylight.h:14` (`PI_D`), `src/hydrology.h:21` and
  `src/plates.h:17` (`PI_F`), `src/gridtest.cpp:13`. Row cosine
  `std::cos(((y + 0.5) / (double)H - 0.5) * 3.14159265)` 5 times in `atmosphere.h`
  (1496, 1607, 2008, 2572, 2792) though `latRad` already holds it, and over `AH` at
  `sweep.cpp:85, 268, 444, 1003` (the earlier count of 14 was wrong; `9f598d3` had 5 too).
- Earth radius: `src/camera.h:33` `camera::EARTH_RADIUS_KM` (formerly `main.cpp:148`),
  `hydrology.h:20` and `plates.h:18` `EARTH_RADIUS_KM`, `atmosphere.h:27` `R_EARTH`,
  `qg2geo.h:40` `A_EARTH`; literal `6371` in `src/sphere.h:33, 44` (`distKm`,
  `moveToward`), `bands.h:96, 123`, `claims.h:164`, `farmland.h:38, 52`
  (`granaryPos`, `farmsteadPos`), `terrain.h:455` (`slopeAt`), `population.h:118, 193-194,
  227`, `settlement.h:799`, `sweep.cpp:186, 220, 227`, `transect.cpp:52-53`, and 22 lines
  of `globe.frag`.
- Lapse: `PRE_LAPSE = 6.5` at `atmosphere.h:725`; literal at 1822, 1986, 3020, 3030, 3049,
  3070, 3107, and `terrain.h:344` (`temperatureC`) (order 02 removed three sites).
- Days per year: `technology.h:12` `YEAR`; literal `365` throughout `population.h`,
  `settlement.h`, `inspect.h`, `main.cpp`, `atmosphere.h`. "Never": `1e18` at
  `settlement.h:641` (`nextTech`), `bands.h:365, 705-706`, and the `1e17` guards in
  `sim::simulate` (`sim.h:141, 144, 152, 154, 162, 207`) vs `technology::INF_T`
  (`technology.h:13`).
- Herd factor `0.85f`: `population.h:363` (`foodTerms`), `population.h:626`
  (`driftAffinity`), `bands.h:57` (`moverCap`); the `technology.h` site went when the diet
  was gathered into `foodTerms` (60f35a6), so three sites remain, not four.
- Vector types: `src/camera.h:17` `camera::Vec3`, `terrain::V3`, `geodesic::D3`;
  conversions at `inspect.h:61-62` (`describePoint`), `main.cpp:373`, `world.h:85`, and
  the `camera::projectToScreen` calls at `overlay.h:459, 468, 558`. `camera::sphereDir`
  (`camera.h:38`) duplicates `atmosphere::unitAt` (`atmosphere.h:2941`) and
  `hydrology::cellDir` (`hydrology.h:46`).
- Thresholds: `MIN_SETTLEMENT_K = 150` (`settlement.h:114`) vs a computable maximum K;
  `80.0f` km at `population.h:228` (`population::build`, founding) and `bands.h:124`
  (`bestProspect`), stated only as "~80 km" in the comment at `population.h:203` — no
  "twice CLAIM_FLOOR_KM x 2" prose exists in the code at this commit;
  `CONTACT_KM = 160` (`settlement.h:779`) "twice the minimum settlement spacing"; `20.0f`
  km arrival radius at `bands.h:422, 432, 504` (`stepBand`); `VILLAGE_FIELDS_KM2 = 58`
  (`settlement.h:345-346`) with its derivation in a comment; sub-step constants
  `population.h:683` (`advance`), `bands.h:216` (`integrateBand`).
- Unit-less names: `Settlement::S, P, R, t, herd, claim[], fuelS` (`settlement.h:600-652`),
  `Band::water` (`settlement.h:702`), `Camera::altitude` in Earth radii (`camera.h:46`), and
  nearly all atmosphere state.
- `enum class` candidates: `panels::Panel::kind`, `tab` (`panels.h:29, 32`),
  `App::genKind`, `debugMode` (`main.cpp:61, 68`), `news::State::level` (`news.h:41`,
  formerly `newsLevel`). The event-queue kinds are done: order 04 made them
  `enum class Due` (`sim.h:112`).
- Line references checked against commit `9f598d3` on 2026-09-15, after orders 01 and 02
  landed; re-checked against `f9c731f` on 2026-09-26.

## Design

`standards/general.md` §Names carry units and §Thresholds and clocks, `standards/cpp.md`
§Numbers. The thresholds in step 3 gate the tables in `Design/Population.md`; the note's
numbers do not change, only how the code states them.

## Recommended change

1. A `units.h` (or a block at the top of `terrain.h`, the root of the include graph) with
  `PI`, `EARTH_RADIUS_KM`, `DAYS_PER_YEAR`, `NEVER_T`, `LAPSE_C_PER_KM`; replace the
  literals file by file, each file one commit, each verified by probe output unchanged.
2. One vector type for CPU geometry; `camera.h`'s `Vec3` (formerly `main.cpp`'s) goes.
3. Rewrite each threshold as an expression over the table it gates, or add a
  `static_assert` that the table can reach it.
4. Rename the unit-less state fields as they are touched by orders 02 and 04, not in a
  sweep of their own. Both are done and left the names as they were, so this step has no
  carrier order now.

## Files

- New: `src/units.h`
- `src/terrain.h`, `src/atmosphere.h`, `src/hydrology.h`, `src/plates.h`, `src/daylight.h`,
  `src/sim.h`, `src/sphere.h`, `src/bands.h`, `src/claims.h`, `src/farmland.h`,
  `src/events.h`, `src/population.h`, `src/settlement.h`, `src/technology.h`,
  `src/qg2geo.h`, `src/camera.h`, `src/inspect.h`, `src/overlay.h`, `src/world.h`,
  `src/panels.h`, `src/news.h`, `src/main.cpp`, `src/sweep.cpp`, `src/transect.cpp`
  (the code that was in `sim.h`, `population.h` and `main.cpp` now lives across these)
- `shaders/globe.frag` (the radius, via order 06's generated constants if that has landed)
- Touches nearly every file the other orders own, so it is queued alone on its night, after
  the orders whose files it shares have merged.

## Done when

- `grep -c "3\.14159" src/*` is zero outside the one definition.
- `MIN_SETTLEMENT_K` and the km spacings are expressions or carry a `static_assert`.
- Probe outputs identical by seed.

## Depends on

Nothing; cheapest done alongside 02 and 04 while those files are open. Both are done and
on `main` (04 since 2026-09-26), so that chance has passed.
