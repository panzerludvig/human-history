# 05 — Carve main.cpp into modules

**Status:** done (2026-09-15)

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

## Run

**Status:** done (pending merge). Branch `wo/05-carve-main`, forked from `4ac16a0`.

Commits, one per step, in the order they were done (the order's steps 1-3 were reordered
because `savefile.h` needs `World`, and `World` needs `Vec3`):

- `965e11e` step 3a, `src/gl.h`
- `b571634` step 3b, `src/camera.h`
- `3c8b1a5` step 2, `src/world.h`
- `cff2ae1` step 1, `src/savefile.h`, `src/test_savefile.cpp`, `build_testsavefile.bat`
- `09d5a3c` step 4, `src/inspect.h`
- `2d3ce73` step 5, `src/bmp.h`; the climate acceptance block deleted (sweep.exe prints it)
- `2578a41` step 6a, `src/textures.h`
- `99c5015` step 6b, `src/overlay.h`
- `68697c8` step 6c, `src/menus.h`, `src/theme.h`
- `18dbbbe` step 6d, `src/panels.h`, `src/news.h`
- `7b2d44e` step 7a, overlay scratch and GDI objects created once
- `b0d3e32` step 7b, `describePoint` into a caller buffer

Files beyond the order's list: `src/textures.h`, `src/overlay.h`, `src/theme.h`,
`src/menus.h`, `src/panels.h`, `src/news.h` (step 6 says "split App into overlay, menu, news
and panel state so those move last"; moving them is what gets `main.cpp` under 1,000, and
the texture uploads had to go somewhere too). `sim.h`, `population.h`, `technology.h` and
`atmosphere.h` untouched.

**Done when, checked:**

1. `main.cpp` is 851 lines (3,600 at `4ac16a0`): includes, `App` (camera, world, screen, the
   generation thread, one `State` per module), the wiring (`setScreen`, `generateWorld`,
   `finishGeneration`, `onCommand`, `updateTooltip`, `goToEvent`, `pickAt`, `advanceDays`,
   the clock), three window procedures, and `main` (window, GL context, uniforms, argv
   harness, render loop). Passes.
2. Screenshots: byte-identical after every one of the twelve commits. Baseline built from
   the untouched `4ac16a0` tree; after each commit `build.bat` (7 warnings each time, the
   pre-existing C4996 set, none new), the four views retaken and hashed. Views, all with
   land 30, concentration 0.5, dir 0, years 0:
   - seed 7, `20 30 12000`: `07E3EBA6E61DEE2C376968CB5B2C09DA540154FA36ADC3BC1563C920A119AF45`
   - seed 7, `45 -10 400`: `3AB8067D06FA77DF8893A8E97A068E6BD65DF313A83FB11AB050B71BEE39924E`
   - earth, `50 10 8000`: `2871E0EBEB98790056B8A80DD71CC1DB1EE18177E76DB64AB4EC906A8177BD0E`
   - earth, `30 35 150`: `8971E759299B4AD3CD74791DB7B8D9A8C8A6B0E5EC963195116455705782CC89`
   Each 2,764,854 bytes (1280x720x24 bit). Passes, 12 of 12 steps, 4 of 4 views.
3. Round-trip probe exists and passes. Passes.

**Commands run** (PowerShell, from the worktree root):

    cmd /c ".uild.bat"
    Start-Process build\humanhistory.exe -ArgumentList "20 30 12000 7 30 0.5 0 0 <shotPath>" -WorkingDirectory build
    # ... poll until <shotPath> exists and its size is unchanged over three half-second polls, then Stop-Process
    Get-FileHash -Algorithm SHA256 build\shot_base_N.bmp, build\shot_<step>_N.bmp   # compared per view
    cmd /c ".uild_testsavefile.bat"
    build	est_savefile.exe 7 3

The argv list in the launch instructions matched `main.cpp`'s harness
(`<lat> <lon> <altKm> <seed> <land%> <conc%> <debugmode> <years> <shotPath>`); note
`conc` is a percentage, so 0.5 is half a percent. The baseline and per-step `.bmp` files
are under `build\` in the worktree (`shot_base_N.bmp`, `shot_<step>_N.bmp`), untracked.

**Probe output** (`build	est_savefile.exe 7 3`, after the final commit):

    saved: seed 7, year 3.0, 401 settlements, 0 bands, 400 cultures, 6 scars, 0 ruins
    roundtrip seed 7 year 3.0: 61352 fields compared, 0 mismatches -- PASS

The first run found one mismatch, `tech.rng`: `savefile::load` restores it and then redraws
every contact and invention clock from it (memoryless, so exact in distribution), which
advances the generator. That is by design, so the probe checks only that the value was
read; everything else the file carries is compared exactly (P re-summed from the cohorts,
to a tolerance).

**Unsure of:**

- Step 7b (`describePoint`) is not covered by the screenshot: the tooltip is a Win32
  control, not part of the frame. The text path was changed mechanically (same format
  strings, same sort comparator, same 5% floor and 60-character stop) and verified by
  reading. Worth a mouse-over in the morning.
- The overlay's winner-per-square `unordered_map` is still a fresh local each redraw: its
  iteration order is the draw order among overlapping markers, and a table reused across
  frames would make a view depend on what was drawn before it. The candidate vector, the
  mark tables and all GDI objects are reused; node allocations in the two mark maps remain.
- `world::activeProgress`: `atmosphere::build` takes a plain function pointer for its
  year-by-year callback, so `World::build`'s progress callback reaches it through a
  namespace-scope pointer set for the duration of the build. Written down in `world.h`;
  the clean fix is a context parameter on `atmosphere::build`, which order 04 owns tonight.
- `gl.h` keeps the GL function pointers at global scope under their API names (deviation
  from one-namespace-per-header, written in the header).
- The `Run:` line in `Technical/Architecture.md` still lists only seven arguments; it was
  already behind before this order and was left alone.
- `build_testsavefile.bat` does not copy `data\` (the probe uses seed 7, not earth), unlike
  `build.bat`; it follows `build_testresources.bat`.
