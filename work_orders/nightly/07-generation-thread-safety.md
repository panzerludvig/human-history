# 07 — Make the world-generation thread safe

**Status:** planned (2026-09-28)

Line references checked against 5087977 on 2026-09-28.

## Problem

World generation runs on a worker thread, and the worker reaches into the main thread's
state and windows:

- **Quitting during generation hangs the game.** The worker reports progress with
  `SetWindowTextA` on a window the main thread owns, which waits for the main thread to
  handle the message. Closing the window ends the main loop, and the main thread then
  blocks in `genThread.join()`, no longer handling messages: the worker's next progress
  call waits forever. Even without the hang, quitting waits for the whole build, since
  nothing can stop it.
- **The worker builds into `app.world` in place** (and the load path writes `app.cam`)
  while the main thread reads `app.world.simTime` for the title bar twice a second. A data
  race; at worst a stale date on screen today, and a trap for anything that reads the
  world during generation later.
- Two comments disagree about which thread builds the world.

Violates `standards/cpp.md` §Hot paths (the render thread is the one hand-rolled thread and
stays in `main.cpp`; that permits it, it does not permit unsynchronised sharing) and
`standards/general.md` §Errors (a data race is a silent wrong value; a hang is not failing
loud).

## Evidence

- `src/main.cpp:41-42` — comment: "The build runs on the UI thread". False.
- `src/main.cpp:56-57` — comment: a worker owns `app.world`. True.
- `src/main.cpp:80-87` — `buildProgress`, called on the worker, calls `SetWindowTextA` and
  `UpdateWindow` (85-86) on the status control.
- `src/main.cpp:515-516` (`WM_CLOSE`) sets `app.running = false`; the loop ends and
  `main.cpp:906` joins the worker without handling messages.
- `src/main.cpp:120-143` (`generateWorld`) — the worker lambda (138-142) builds
  `app.world` in place. `src/main.cpp:190-203` (`onCommand`, `ID_LOAD_CONFIRM`) — the
  worker lambda (199-201) passes `app.world` and `app.cam` to `savefile::load`
  (`src/savefile.h:104`).
- `src/main.cpp:404` (`simDate`) reads `app.world.simTime`; the title bar calls it at 899
  on the main thread twice a second, on every screen.
- `src/main.cpp:59` — the atomic `App::genState` already marks the worker finished, and
  `finishGeneration` (147-148) runs on the main thread: the handover point exists, the worker
  just does not stay on its side of it.
- `src/world.h:58-61` — `world::activeProgress`, a namespace-scope pointer the worker sets
  so `atmosphere::build`'s plain function-pointer callback (`src/atmosphere.h:2667`,
  reported every 15 days at 2760) can reach the caller's progress function. Order 05
  recorded it as a stand-in for a context parameter.
- `src/world.h:109-165` — `World::build` and its stages; nothing can stop it between or
  inside them.
- `terrain::TEMPLATE` (`src/terrain.h`) is global and the build sets `TEMPLATE.active`;
  the main thread reads it only while drawing the globe, which no generation screen does.

## Design

`standards/cpp.md` §Hot paths and `standards/general.md` §Errors. `Technical/Globe
Viewer.md` describes the generation flow and changes to say which thread owns what.

Decided 2026-09-28: **closing the window during generation cancels the build.** No
interface change; closing is the only trigger.

## Outcome

- **Closing the window during generation cancels the build** and the game exits promptly,
  for a new world and for a load. No interface change.
- **The worker owns what it builds.** Nothing it runs reads or writes the main thread's
  state or touches a window; the finished world (and, for a load, the camera) reaches the
  game on the main thread, at the handover `finishGeneration` already provides. Progress
  text still reaches the status line.
- **The build can be stopped from outside and reports progress without a global.**
  `world::activeProgress` is gone. (Suggestion: a context argument to `World::build` and
  `atmosphere::build` carrying the progress function and a cancel flag, checked between
  stages and where the climate reports progress.)
- **The argv test path can generate through the worker**, so the handover is testable
  (`HH_GENTEST`, below).
- The false comment is gone and `Technical/Globe Viewer.md` says which thread owns what
  during generation.

## Files

A guide, not a limit:

- `src/main.cpp` (`buildProgress`, `generateWorld`, `finishGeneration`, the load path,
  `WM_CLOSE`, the argv test path)
- `src/world.h` (`World::build`, the context, `activeProgress` removed)
- `src/atmosphere.h` (`build`'s signature and its progress/cancel check)
- `src/savefile.h` (`load` takes the context and fills the world and camera it is given)
- `src/test_atmo.cpp` (calls `atmosphere::build`) and `src/test_savefile.cpp` (calls
  `World::build` with a callback): signature only
- `Technical/Globe Viewer.md`

## Done when

1. `build.bat` and every `build_*.bat` exit 0 with no warning beyond the existing C4996
   set.
2. The world built through the worker is the world built directly. With the environment
   variable `HH_GENTEST=1`, the argv test path builds through `generateWorld` and
   `finishGeneration` on the worker instead of calling `World::build` on the main thread.
   The screenshots from `build\humanhistory.exe 46.318 -174.111 4 7 30 60 0 0 <shot>` and
   `20 30 12000 7 30 60 0 0 <shot>`, with and without `HH_GENTEST=1`, are byte-identical.
3. Quitting mid-build returns. With `HH_GENTEST=1` and `HH_GENTEST_CLOSE_MS=3000` (the game
   posts `WM_CLOSE` to itself that long after generation starts), the process exits within
   6 seconds of launch, for seed 7 and for `earth`. Run three times each; the Run section
   quotes the times.
4. `build\sweep.exe 7 0 rules 1` and `build\test_resources.exe 7 40` are byte-identical to
   the pre-order baseline: the build computes the same world.
5. `grep -n "app\." ` over the bodies of the two worker lambdas finds only the handover
   slot, and `grep -rn "activeProgress" src` finds nothing. The Run section quotes both.

## Depends on

Nothing.
