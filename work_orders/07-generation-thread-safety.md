# 07 — Make the world-generation thread safe

**Status:** open (2026-09-26) — 05 is merged and `World` has its own header, so nothing
blocks it

Line references re-checked against f9c731f on 2026-09-26.

## Problem

The generation worker touches UI windows and writes `app.world` and `app.cam` while the
main loop reads `app.world.simTime` for the title bar twice a second. Two comments in the
file contradict each other about which thread builds the world.

Violates `standards/cpp.md` §Hot paths (the render thread is the one hand-rolled thread
and stays in `main.cpp`; that permits it, it does not permit unsynchronised sharing) and
`standards/general.md` §Errors (a data race is a silent wrong value).

## Evidence

- `src/main.cpp:40-41` — comment: the build runs on the UI thread.
- `src/main.cpp:55-56` — comment: a worker owns `app.world`.
- `src/main.cpp:57` — `App::genThread`; created at 137 (`generateWorld`) and 198
  (`onCommand`, `ID_LOAD_CONFIRM`), joined at 147 (`finishGeneration`) and 883 (exit).
- `src/main.cpp:79-86` — `buildProgress` calls `SetWindowTextA` and `UpdateWindow` (84-85)
  from the worker.
- `src/main.cpp:198-199` — the load path hands `app.world` and `app.cam` to
  `savefile::load` (`src/savefile.h:104`), which writes both from the worker.
- `src/main.cpp:403` — `simDate()` reads `app.world.simTime`; the title bar calls it at 876
  on the main thread twice a second regardless of screen.
- Since this was written, an atomic `App::genState` (`main.cpp:58`) marks the worker
  finished and `finishGeneration` (146) runs on the main thread; that orders the handover
  at the end, but the worker still builds into `app.world` in place, so the reads above
  still race it.
- Line references checked against commit `9f598d3` on 2026-09-15, after orders 01 and 02
  landed; re-checked against `f9c731f` on 2026-09-26.

## Design

`standards/cpp.md` §Hot paths (the one hand-rolled thread) and `standards/general.md`
§Errors. The generation flow is described in `Technical/Globe Viewer.md`, which changes to
say which thread builds the world and how the handover works.

## Recommended change

1. The worker builds into a `World` it owns and hands it over through one atomic flag or a
  `PostMessage` to the main thread, which then swaps it into `app.world` and sets the
  camera. Nothing on the main thread reads `app.world` while the flag says building.
2. `buildProgress` posts a message with the text; the main thread updates the window.
3. Delete whichever of the two comments is false.

Best done after order 05 step 2 gives `World` its own header, since the handover is then a
function over `World` rather than over `App`. That has happened: `world::World` is in
`src/world.h`, with `World::build` at `world.h:109`.

## Files

- `src/main.cpp` (worker creation and joins, `buildProgress`, the load path, `simDate`)
- `src/world.h` (`World::build`)
- `src/savefile.h`, if the handover changes what `savefile::load` is given
- `Technical/Globe Viewer.md`
- Order 05, which shared `main.cpp`, is merged; of the open orders only 09 still lists `main.cpp`.

## Done when

- No function called from the worker touches a window handle or a field of `App` other
  than the handover slot.
- The two comments agree with the code.
- Generating a world and switching screens during generation produces no stale title or
  camera.

## Depends on

05 (step 2), preferably. Merged into `main` on 2026-09-26, so nothing now.
