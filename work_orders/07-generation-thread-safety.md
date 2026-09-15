# 07 — Make the world-generation thread safe

**Status:** open (2026-09-15) — waits for 05 to be merged into `main`

## Problem

The generation worker touches UI windows and writes `app.world` and `app.cam` while the
main loop reads `app.world.simTime` for the title bar twice a second. Two comments in the
file contradict each other about which thread builds the world.

Violates `standards/cpp.md` §Hot paths (the render thread is the one hand-rolled thread
and stays in `main.cpp`; that permits it, it does not permit unsynchronised sharing) and
`standards/general.md` §Errors (a data race is a silent wrong value).

## Evidence

- `src/main.cpp:277-278` — comment: the build runs on the UI thread.
- `src/main.cpp:794-796` — comment: a worker owns `app.world`.
- `src/main.cpp:796, 1869, 1918` — `app.genThread` creation and joins.
- `src/main.cpp:1616` — `buildProgress` calls `SetWindowTextA` and `UpdateWindow` from the
  worker.
- `src/main.cpp:1919` — the load path writes `app.cam` from the worker.
- `src/main.cpp:3587` — `simDate()` reads `app.world.simTime` on the main thread regardless
  of screen.
- Line references checked against commit `9f598d3` on 2026-09-15, after orders 01 and 02
  landed.

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
function over `World` rather than over `App`.

## Files

- `src/main.cpp` (worker creation and joins, `buildProgress`, the load path, `simDate`)
- `src/world.h`, once order 05 has created it
- `Technical/Globe Viewer.md`
- Same file as order 05, so it runs on a later night.

## Done when

- No function called from the worker touches a window handle or a field of `App` other
  than the handover slot.
- The two comments agree with the code.
- Generating a world and switching screens during generation produces no stale title or
  camera.

## Depends on

05 (step 2), preferably.
