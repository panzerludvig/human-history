# 09 — One name for each shared constant; thresholds derived from their tables

**Status:** open (2026-09-15)

## Problem

Pi is spelled as a literal about 180 times in five spellings. Earth radius, days per year,
the 6.5 lapse rate, the row-cosine expression, the 0.85 herd factor and "never" as a time
each recur ten to twenty times with a named constant already present somewhere. Three
vector types exist. Several thresholds are literals compared against tables whose maximum
is computable, which is the recorded bug class from 2026-08-23.

Violates `standards/general.md` §Names carry units and §Thresholds and clocks, and
`standards/cpp.md` §Numbers.

## Evidence

- Pi: `3.14159265` about 135 sites, `3.14159265f` 33, three other spellings; named only at
  `src/qg2geo.h:43` and `src/main.cpp:147`. Row cosine
  `std::cos(((y + 0.5) / (double)H - 0.5) * 3.14159265)` 17 times in `atmosphere.h`
  though `latRad` already holds it.
- Earth radius: `main.cpp:148` `EARTH_RADIUS_KM`, `atmosphere.h` `R_EARTH`, literal
  `6371` in `sim.h:58, 597, 611`, `terrain.h:449`, `population.h` (5 sites),
  `sweep.cpp:362`, `globe.frag`.
- Lapse: `PRE_LAPSE = 6.5` at `atmosphere.h:766`; literal at 1885, 1932, 1976, 2099, 3293,
  3303, 3322, 3343, 3380.
- Days per year: `technology.h:11` `YEAR`; literal `365` throughout `population.h`,
  `sim.h`, `main.cpp`. "Never": `1e18`/`1e17` at `population.h:617`, `sim.h:1350-1351,
  1432, 1443-1451` vs `technology::INF_T`.
- Herd factor `0.85f`: `technology.h:147`, `sim.h:375, 524`, `population.h:1392`.
- Vector types: `main.cpp:137 Vec3`, `terrain::V3`, `geodesic::D3`; conversions at
  `main.cpp:1104, 1989-1990, 3062`. `sphereDir` (153) duplicates `atmosphere::unitAt`
  and `hydrology::cellDir`.
- Thresholds: `MIN_SETTLEMENT_K = 150` (`population.h:108`) vs a computable maximum K;
  `80.0f` km at `population.h:1018` and `sim.h:441` described as "twice CLAIM_FLOOR_KM
  x 2" in prose; `CONTACT_KM = 160` (747) "twice the minimum spacing"; `20.0f` km arrival
  radius at `sim.h:995, 1006, 1076`; `VILLAGE_FIELDS_KM2 = 58` (`population.h:338`)
  with its derivation in a comment; sub-step constants `population.h:1229`, `sim.h:711`.
- Unit-less names: `Settlement::S, P, R, t, herd, claim[], fuelS` (`population.h:574-627`),
  `Band::water` (678), `Camera::altitude` in Earth radii (`main.cpp:163`), and nearly all
  atmosphere state.
- `enum class` candidates: `Panel::kind`, `tab`, `debugMode`, `genKind`, `newsLevel`
  (`main.cpp:826, 3215, 3339`); event kinds (`sim.h:1432`).

## Design

`standards/general.md` §Names carry units and §Thresholds and clocks, `standards/cpp.md`
§Numbers. The thresholds in step 3 gate the tables in `Design/Population.md`; the note's
numbers do not change, only how the code states them.

## Recommended change

1. A `units.h` (or a block at the top of `terrain.h`, the root of the include graph) with
  `PI`, `EARTH_RADIUS_KM`, `DAYS_PER_YEAR`, `NEVER_T`, `LAPSE_C_PER_KM`; replace the
  literals file by file, each file one commit, each verified by probe output unchanged.
2. One vector type for CPU geometry; `main.cpp`'s `Vec3` goes.
3. Rewrite each threshold as an expression over the table it gates, or add a
  `static_assert` that the table can reach it.
4. Rename the unit-less state fields as they are touched by orders 02 and 04, not in a
  sweep of their own.

## Files

- New: `src/units.h`
- `src/terrain.h`, `src/atmosphere.h`, `src/hydrology.h`, `src/sim.h`, `src/population.h`,
  `src/technology.h`, `src/qg2geo.h`, `src/main.cpp`, `src/sweep.cpp`
- `shaders/globe.frag` (the radius, via order 06's generated constants if that has landed)
- Touches nearly every file the other orders own, so it is queued alone on its night, after
  the orders whose files it shares have merged.

## Done when

- `grep -c "3\.14159" src/*` is zero outside the one definition.
- `MIN_SETTLEMENT_K` and the km spacings are expressions or carry a `static_assert`.
- Probe outputs identical by seed.

## Depends on

Nothing; cheapest done alongside 02 and 04 while those files are open.
