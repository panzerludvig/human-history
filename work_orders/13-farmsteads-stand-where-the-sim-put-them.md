# 13 — Farmsteads stand where the simulation says they stand

**Status:** open (2026-09-18)

## Problem

Three defects in the farmstead layer, all from the 2026-09-14 session, two of them accepted
at the time to keep the frame-rate fix simple. They share a cause — the slot spiral is
computed in three places that do not agree about what a slot is — so they are one order.

1. **Houses on water.** The simulation prices a farmstead slot at its own cell's
  suitability, so a slot on a lake opens no farmland; the shader draws its farmhouse and
  yard anyway. A farmstead standing in open water is visible today at any close zoom over a
  lakeside village.
2. **One back-pointer per cell.** The pop texture's alpha carries a single village cell per
  20 km cell. Two villages whose farmsteads fall in the same cell overwrite each other, and
  the loser's farmhouses and far fields vanish from the map. Accepted on 2026-09-15 as rare;
  it is not rare once claims interlock.
3. **A bad slot stops the spiral forever.** `fsteadNextOk` tests only slot `n`, so a
  settlement whose next slot falls on water or scree never builds another farmstead, even
  when slots `n+1..19` are prime grass. The spiral was meant to be taken in order because of
  the claim border, not because one bad cell ends farming expansion.

Violates `standards/general.md` §Mirrored code: `farmsteadPos` exists on the CPU
(`farmland.h`) and in the shader twice, and the CPU's suitability test has no mirror.

## Evidence

- `src/farmland.h:46-54` — `farmsteadPos`, the CPU definition, marked as mirrored in
  `shaders/globe.frag`.
- `src/farmland.h:86` — `s.fsteadNextOk = n < slots && pf.sFarmMap[cellOf(farmsteadPos(
  s.cell, n))] > 0.05f;` — defect 3: only slot `n` is ever considered.
- `src/farmland.h:68-73` — the built slots are priced at their own cell (`sFarmMap`), which
  is the rule the drawing does not know about (defect 1).
- `src/textures.h:73-75` — the back-pointer write, last writer wins (defect 2).
- `shaders/globe.frag` — `hutsNear`'s farmstead block draws a house and yard for every slot
  `k < site.a` with no land test; `fieldsNear`'s farmstead block does the same for fields.
  Both read the back-pointer written above.
- `src/inspect.h:166` — the tooltip pick walks the same slots and names a farmstead on
  water too.

Line references checked against commit `8d974f3` on 2026-09-18.

## Design

[[Design/Technology]] §Farming's reach, tilled plots, and farmsteads, at status
Implemented: "Its land is priced at **its own cell's** suitability, not the village's — the
river village whose claim runs into hills gets farmsteads only where the grass is, and a
slot that falls on scree or water opens nothing, which is the map talking." Defects 1 and 3
are the code failing that sentence in two directions: the map shows what the note says does
not exist, and the sim stops at what the note says should be skipped.

Defect 2 is `standards/general.md` §Mirrored code — one encoding shared by CPU and GPU that
cannot represent the state.

One design question the run may not decide: whether a settlement **skips** an unsuitable
slot (keeping the spiral's positions, leaving gaps) or **compacts** past it (renumbering, so
slot 3 moves outward). Skipping keeps `farmsteadPos(cell, k)` a pure function of `k`, which
is what the three mirrors depend on, so the recommended change assumes skipping; say so in
the design note when this lands.

## Recommended change

1. **Slot validity in one place.** A `farmsteadSlotOk(pf, s, k)` in `farmland.h` that tests
  the slot's cell: land (not sea, not lake), inside the claim, suitability > 0.05.
  `updateFarmland` uses it for `fsteadMax`, and `fsteadNextOk` scans forward for the first
  valid unbuilt slot rather than testing only the next one.
2. **Build into the valid slot.** `Settlement` records which slot each standing farmstead
  occupies (a 20-bit mask, or a `uint8_t` next-slot cursor plus the mask), so a skipped slot
  stays empty and the drawn positions keep matching `farmsteadPos(cell, k)`.
3. **The texture carries the mask**, replacing the count in `site.a`: the shader draws slot
  `k` only if its bit is set, which fixes defect 1 without a land test in the shader — the
  CPU stays the source of truth, as the standard requires.
4. **The back-pointer holds up to two villages** (alpha packs two cell indices, or a second
  channel is taken from the band slot, which is free at close zoom), and the writer keeps
  the two nearest rather than the last. Two is enough for interlocking claims; three in one
  20 km cell is a deliberate accepted limit, written down where the packing is defined.
5. The tooltip pick reads the mask too.

## Files

- `src/farmland.h` (slot validity, `updateFarmland`)
- `src/settlement.h` (the occupied-slot mask)
- `src/population.h` (`stepBuilding` sets the mask when a farmstead finishes)
- `src/textures.h` (site texture, back-pointer packing)
- `shaders/globe.frag` (`hutsNear`, `fieldsNear` — both farmstead blocks)
- `src/inspect.h` (tooltip pick)
- `src/savefile.h` (the mask is state; version bump and back-fill)
- `src/test_savefile.cpp`, `src/test_resources.cpp` (the assertions below)

## Done when

- `build.bat`, `build_testresources.bat` and `build_testsavefile.bat` all build.
- `build\test_resources.exe 7 700` prints, and both are **zero**: the number of standing
  farmsteads whose cell is sea or lake, and the number of settlements with an unbuilt valid
  slot beyond a skipped invalid one that never built again.
- The probe prints the number of 20 km cells holding farmsteads of three or more different
  settlements; it is quoted, and the packing is documented as covering two.
- `build\test_savefile.exe` passes with the slot mask round-tripping.
- Farmstead totals at year 700 are at or above the pre-change 59: skipping bad slots can
  only let more be built.

## Depends on

Nothing. Shares `src/population.h` with order 11 and `src/farmland.h` with order 12; not the
same night as either.
