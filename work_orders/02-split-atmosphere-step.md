# 02 — Split atmosphere `step` into named stages

**Status:** done (2026-09-15) — see `Dev Log/Log.md` 2026-09-15

Rewritten 2026-09-15 after order 01 landed: the line references were off by 60 to 300
lines, the sub-model drivers in `prescribeHour` were handed to order 03, and the two
"done when" criteria that contradicted each other (skip stages in the game, keep the sweep
output identical) are resolved by a probe flag.

## Problem

`Model::step` is 581 non-blank lines and `prescribeHour` 270. In the game build most of
`step` is computed and discarded: every hour runs the sea-level reduction, the upper-pool
mean, divergence and orography, two-layer radiation, sensible heat, the surface and ice
balance, the layer budgets and the uplift by cause, then one line overwrites the
temperatures and the ice with the painted values. What survives an hour when the climate
is painted is the evaporation, the soil, the humidity mirror, the cloud fraction and the
rule rain. The rule rain itself reads only static geography and the day of year. The rest
exists to fill the sweep's probes (uplift by cause, the tropical column budget, the zonal
budget, the energy-conservation line). The game runs three years of hourly steps of it at
world-generation time.

`prescribeHour` is the painting plus one driver each for DYN2, QG2 and QG2GEO+WATER2 under
one name. The drivers are order 03's to keep or remove; this order does not extract them,
since extracting a driver that then leaves is wasted work.

Violates `standards/cpp.md` §Shape (a function reads in one screen; an over-limit function
may not grow without extracting first) and §Hot paths (a static buffer resized inside
`step`; per-hour full-vector copies to feed a probe or to undo work).

## Evidence

Verified 2026-09-15 against the working tree after order 01.

- `src/atmosphere.h:2024-2608` — `step`; `:2571` the overwrite `nT = T; nTb = Tb; ...`.
- Seams, each already a comment block: 2035-2047 sea-level reduction; 2049-2068 upper-pool
  mean; 2077-2089 prognostic dynamics (only when `!PRESCRIBED`); 2091-2162 divergence and
  orography; per-cell loop 2165-2543 in six stages (solar and radiation 2190-2264, surface
  exchange and evaporation 2265-2309, surface and ice 2335-2371, layer budgets 2384-2437,
  uplift 2471-2527, water 2528-2541); polar caps and polar filter 2544-2570; conservation
  probe 2576-2604.
- Probe blocks: tropical column budget 2375-2383 and 2439-2451; zonal budget 2452-2465;
  single-cell probe 2466-2470; conservation 2576-2604.
- Per-hour copies: 2075-2076 (`hPrev`, `TbPrev`, read only by the probes and the
  dynamics) and 2571.
- `src/atmosphere.h:2129-2130` — `static std::vector<double> sm; sm.resize(W * H);` inside
  `step`, swapped with the member `div`.
- `src/atmosphere.h:1753-2022` — `prescribeHour`; the painting 1760-1823; drivers 1825-2021
  (order 03).
- What reads the discarded stages' output outside `step`: `build` 2687-2697 (uplift by
  cause, evaporation terms) and 2710-2733, 2804-2818 (probes), all printed by the sweep;
  `main.cpp:3386` reads only `dbgEvap` and `dbgRain`.

## Recommended change

In order, each its own commit, each verified by the sweep's `rules` output being
unchanged for two seeds:

1. Extract the probe blocks of `step` into named functions (no behaviour change). Move
  the static buffer to `init`.
2. Extract the per-cell stages into named functions taking the cell index, an hour
  context and a per-cell record of the hour's terms; `step` becomes a list of calls.
3. Extract the painting out of `prescribeHour` into its own function. The drivers stay
  where they are.
4. Add a `PROBES` flag. The sweep sets it and runs every stage, so its output is
  unchanged. The game leaves it off and, when `PRESCRIBED`, runs only the stages whose
  output survives the overwrite. A sweep mode `game` runs the game's stage set so the
  surviving figures can be compared against the full run. Measure the climate run's time
  before and after with the game's configuration (`build\sweep.exe earth 1 rules 2`: one
  spin-up year, two averaged) and record it in `Technical/Globe Viewer.md`.

Out of scope, noted for its own order: `build` (2611-2908) is 297 non-blank lines with the
month-end probe prints inline, and `stepDynamics` is 138.

## Done when

- `step` and `prescribeHour` are each a list of named calls, and no function either of
  them calls exceeds about 120 non-blank lines.
- `build\sweep.exe earth 0 rules 1` and `build\sweep.exe 7 0 rules 1` print the same
  output before and after, to every digit.
- `build\sweep.exe earth 0 game 1` prints the same climate figures (the `CLIMATE` block,
  the water line, the rules report) as `rules`; only the probe lines may differ.
- The climate run's time with the game's configuration is recorded in
  `Technical/Globe Viewer.md` before and after.
- `build.bat` succeeds with no new warnings and the game's climate is unchanged by seed.

## Depends on

01. Order 03 owns the sub-model drivers in `prescribeHour`; this order does not touch them.
