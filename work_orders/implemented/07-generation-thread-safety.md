# 07 — Make the world-generation thread safe

**Status:** implemented (2026-09-29): kept and merged into `main` as `5ca4701`

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

## Run

**Outcome:** passed. Branch `wo/07-generation-thread-safety`, one commit `63b8cc9`.
Merged into `nightly` as `a7c6968`, after 06. `src/main.cpp` conflicted only because 06
added `globeConstants` and 07 the argv test-path functions at the same place after
`wndProc`; both kept as written.

What it did: `src/progress.h` (new), `progress::Context`, a report function and a cancel
flag. `World::build`, `atmosphere::build` and `savefile::load` take it and return false
when cancelled; `atmosphere::build` checks once per simulated day, `World::build` before
each stage. `world::activeProgress` is gone. `App::gen` is the handover slot: the worker
touches only it, the main loop shows its label, `finishGeneration` moves the world and
the loaded camera into `app` on the main thread. `WM_CLOSE` sets cancel. `HH_GENTEST=1`
builds the argv world through the worker; `HH_GENTEST_CLOSE_MS` posts `WM_CLOSE`.

Done when:

1. All ten `build*.bat` exit 0, C4996 only.
2. Screenshots identical pre-order, direct and through the worker: `46.318 -174.111 4 7
   30 60 0 0` EFF96727..., `20 30 12000 7 30 60 0 0` 95E99F28...; on `nightly`, through the
   worker, identical to 06's (c9d5b918, 54b19e8c).
3. Quit mid-build, three runs each, machine at 100% CPU: seed 7 4.81, 4.55, 4.45 s; earth
   4.52, 4.37, 4.55 s. On `nightly`: 3.65, 3.64, 3.65 s and 3.64, 3.67, 3.66 s, exit 0.
4. `sweep.exe 7 0 rules 1` and `test_resources.exe 7 40` byte-identical to the pre-order
   run; `test_savefile.exe 7 3` 61352 fields, 0 mismatches.
5. The worker lambdas hold only `app.gen.*` (`app.gen.ok = app.gen.world.build(ctx);`,
   `app.gen.ok = savefile::load(name, app.gen.world, app.gen.cam, ctx);`); `grep -rn
   activeProgress src` finds nothing.

Files beyond the list: `src/progress.h`, `Technical/Architecture.md` (its module list).

Unsure of: `progress::Context` has its own header rather than living in `world.h`.
Plates, sea level, hydrology and settlements cannot be cancelled mid-stage. On a load
only latitude, longitude and altitude are handed to `app.cam`. The screenshot runs
through the worker took 53 and 101 s against 846 and 1078 s on the main thread, launched
together, images identical: unexplained, possibly OpenMP on the main thread. For the
game: the status line during New World and Load, closing from the menus, a loaded world's
camera.

## Review

**Keep**, 2026-09-29. Merged into `main` (`5ca4701`), with the whole night, on the developer's acceptance. Still to see in the game: the status line, closing during generation from the menus, a loaded world's camera. The tenfold faster build through the worker thread is unexplained.

Review note for the night: [[Dev Log/Nightly/2026-09-28]].
