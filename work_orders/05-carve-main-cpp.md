# 05 — Carve main.cpp into modules

**Status:** queued (2026-09-15)

## Problem

`main.cpp` is 3,600 lines holding thirteen concerns behind one global `App`: GL loading,
shader build, camera, world build, save and load, texture packing, GDI overlay, Win32
menus, news feed, detail panels, panel text, sim clock, and a `main` that is window setup
plus an argv test harness plus the render loop plus a BMP writer. The global is the
obstacle: functions read `app.` instead of taking parameters.

Violates `standards/general.md` §Modules and `standards/cpp.md` §Shape.

## Evidence

- Save and load: `saveWorld` 365-427, `loadWorld` 429-737, `listWorlds` 739-751. Pure over
  `World&` and `Camera&`, twenty `version >= N` branches, no probe covers them. Only UI tie
  is `buildProgress` (declared 279, defined 1616).
- World build: `struct World` 283-361; `12000.0f` river threshold at 335 and 354.
- Panel text: `peopleText` 2236, `envText` 2294, `historyText` 2367, `techDetailText`
  2394, `buildingsText` 2499, `bandText` 2558, `panelContent` 2590, `describeMixture`
  1972, `describePoint` 1987. All read `app.world`.
- GL loader and shader build 31-133 and 219-273: no `app` dependency.
- Vec3 and Camera 135-217; `zoomAt` 1203, `projectToScreen` 1103, `applyDrag` 3120.
- Texture packing `popTexData` 861, `siteTexData` 899, `bandTexData` 919: pure over
  `population::Field`.
- Overlay 932-1456; menus 758-767, 1632, 1707-1778, 1895-1963; news 2613-2885; panels
  2887-3084; clock `simDate` 3091, `advanceDays` 3107.
- `main` 3228-3600: setup 3229-3322, argv harness 3324-3424 (a block marked "temporary
  instrumentation" at 3370-3408), render loop 3446-3577, BMP writer 3549-3573.
- Per-frame allocation: `paintOverlay` 1303-1341 allocates three containers and six GDI
  objects per redraw and `overlayStale` (1437) is true on every camera-move frame;
  `describePoint` allocates on every mouse move (3199).
- Line references checked against commit `9f598d3` on 2026-09-15, after orders 01 and 02
  landed.

## Design

`standards/general.md` §Modules, `standards/cpp.md` §Shape and §Hot paths. The module
list in `Technical/Architecture.md` and the viewer description in
`Technical/Globe Viewer.md` change in the same commits.

## Recommended change

Extractions in order of least risk, each a commit with no behaviour change, verified by
the F2 screenshot being pixel-identical for a fixed seed and view:

1. `savefile.h`: save, load, list, taking a progress callback. Add a probe that round-trips
  a world and compares fields, since twenty version branches have no test.
2. `world.h`: `World` with `derive` and `build`, same callback. Order 08 then makes the
  probes use it.
3. `gl.h`: loader and shader build. `camera.h`: Vec3, Camera, zoom, project, drag with a
  `Camera&` parameter.
4. `inspect.h`: the panel text functions with a `const World&` parameter.
5. `bmp.h`: the writer; delete the temporary instrumentation block or give it a flag.
6. Split `App` into overlay, menu, news and panel state so those move last.
7. Give `paintOverlay` a scratch struct sized once; give `describePoint` a reusable buffer.

## Files

- `src/main.cpp`
- New: `src/savefile.h`, `src/world.h`, `src/gl.h`, `src/camera.h`, `src/inspect.h`,
  `src/bmp.h`
- New probe: `src/test_savefile.cpp`, `build_testsavefile.bat`
- `Technical/Architecture.md`, `Technical/Globe Viewer.md`
- Owns `main.cpp` for the night it runs; 06 and 07 also touch it and are queued on other
  nights.

## Done when

- `main.cpp` is under 1,000 lines and holds only Win32 plumbing, the render loop and the
  wiring between modules.
- F2 screenshots for two seeds and views are byte-identical before and after. Headless:
  the tenth argument (`main.cpp:3421`) saves a frame to that path and keeps running, so the
  probe waits for the file and then kills the process.
- A save/load round-trip probe exists and passes.

## Depends on

Nothing. Runs in parallel with 04.
