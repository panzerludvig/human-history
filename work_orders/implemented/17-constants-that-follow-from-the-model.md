# 17 — Constants that follow from the model

**Status:** implemented (2026-09-29): kept and merged into `main` as `5ca4701`

Line references checked against 827c828 on 2026-09-28.

## Problem

Several numbers in the population model stand where the model already has an answer: a
fixed capacity floor for founding where the settlers' own number decides whether a place
can hold them, a fixed contact radius beside a modelled awareness range, a fixed shortest
move beside the movers' own reach, an arrival radius that is the grid's cell size written
out, and integration caps that quietly lengthen the step when a span is long. Each can
disagree with the thing it stands for without anyone noticing, which is the bug class of
2026-08-23 (a settlement threshold the yield table could not reach).

Violates `standards/general.md` §Thresholds and clocks, which since 2026-09-28 also says a
constant exists only for what the model does not model, and §Errors (a clamp that hides a
value going out of range).

## Evidence

- **`MIN_SETTLEMENT_K = 150`** (`src/settlement.h:114`): the capacity a place must have to
  be settled. Move search (`src/bands.h:115-120`, `161`, `bestProspect`): water, forager
  capacity and mover capacity must each reach 150. Founding (`bands.h:505-507, 522-524`):
  mover capacity must reach 150 besides `canHold` (the ground holds the band itself). The
  comment at `bands.h:495-497` records that settling used to be judged against a fixed
  threshold while leaving was judged against the group, which `canHold` fixed; the floor
  stayed. World start (`src/population.h:209`) goes with order 21. `Design/Population.md`
  still says "K ≥ 800".
- **`CONTACT_KM = 160`** (`settlement.h:779`, "twice the minimum settlement spacing"):
  `computeNeighbours` (`settlement.h:787-805`) and a new settlement's neighbours
  (`bands.h:307-316`) make the contact network; it carries awareness and practice
  (`src/technology.h:232, 239`), the carriers count for losing a skill
  (`technology.h:393`), raid targets (`src/raids.h:99`), and where a failing band merges
  (`bands.h:339`, `mergeBand`). The panels print it (`src/inspect.h:505, 533`).
- **The awareness range** it becomes: `settlementAwareKm`, `bandAwareKm`
  (`settlement.h:231-240`).
- **The shortest move**: `bestProspect` skips a prospect within 80 km of where the movers
  stand (`bands.h:124`).
- **The arrival radius**: `20.0f` km at `bands.h:422, 432, 504` (`stepBand`), the size of a
  hydrology cell written out.
- **The sub-step caps**: settlements integrate in steps of 5 days capped at 800
  (`population.h:683`, `advance`); bands in steps of 2 days capped at 60
  (`bands.h:216`, `integrateBand`). Past 4,000 and 120 days the steps lengthen silently.

## Design

`standards/general.md` §Thresholds and clocks and §Errors. [[Design/Technology]] §Spread
and [[Design/Conflict]], designed 2026-09-28: contact is awareness. Decided 2026-09-28:

- A place is worth settling if it can hold the people who would settle it.
- Contact is awareness; the faster spread of knowledge that follows is accepted, and order
  25 recalibrates the spread.
- The shortest move follows from the movers' own reach; arrival is being in the target's
  cell; integration never lengthens its step.
- Left to other orders: world start (21), the farming reach and `VILLAGE_FIELDS_KM2` and
  the claim floor and cap (24).

## Outcome

- **`MIN_SETTLEMENT_K` is gone.** The move search judges a place by whether it can hold the
  movers (their number is known to the search); a band founds where the ground can hold it
  (`canHold`), as today. `Design/Population.md` no longer names a capacity floor.
- **`CONTACT_KM` is gone.** Two settlements are in contact when either lies within the
  other's awareness range. Contact carries awareness and practice, the carriers count for
  losing a skill, and raid targets. A failing band merges into the nearest settlement
  within its own awareness range. The contact network follows awareness as it grows with a
  settlement's age. The panels show contact as awareness.
- **The shortest move is the movers' own claim reach**: a prospect inside the ground they
  already hold is not a move.
- **Arrival is being in the target's cell**: the radius is computed from the grid.
- **Integration never lengthens its step**: a long span takes more steps of the same
  length, for settlements and for bands.
- The design and Technical notes that describe these say what they now follow from.

## Files

A guide, not a limit: `src/settlement.h`, `src/bands.h`, `src/population.h`,
`src/technology.h`, `src/raids.h`, `src/sim.h`, `src/inspect.h`, `src/test_resources.cpp`,
`Design/Population.md`, `Design/Migration.md`.

## Done when

1. `build.bat` and every `build_*.bat` exit 0 with no warning beyond the existing C4996
   set.
2. `grep -rn "MIN_SETTLEMENT_K\|CONTACT_KM" src` finds nothing, and none of the literals
   above (80 km in `bestProspect`, 20 km arrival, the 800 and 60 step caps) remains at its
   site. The Run section quotes the greps.
3. **The caps are measured before they go**: the pre-order run counts how often each cap
   bound (settlement spans over 4,000 days, band spans over 120), and the Run section
   quotes the counts.
4. **The world is measured**: `build\test_resources.exe 7 700` and `3 700`, before and
   after, with the Run section quoting settlements, people, bands, settlements practising
   each technology, and raids. A guard against collapse, not a target: people at year 700
   are at least 70% of the pre-order run.
5. **The cost is measured**: the wall-clock time of those runs before and after is quoted;
   the after-time is at most twice the before-time.

## Depends on

Nothing.

## Run

**Outcome:** passed. Branch `wo/17-constants-that-follow-from-the-model`, commits
`52b3162` (probe reporting), `5062b6d` (fixed-length steps), `6ad9412` (settling and
contact). The subagent first handed back unfinished and uncommitted, saying it had been
told to; it was resumed and finished. Merged into `nightly` as `419ece8`, after 12.
Conflicts, judged independent and resolved with lines as written: `src/inspect.h`, 11's
builders line inserted above the awareness line 17 rewrote; `src/test_resources.cpp`,
11 and 12's report lines beside 17's, and the header's paragraphs, 12's run line and
17's `#include <chrono>`. Deferred at first under the old rule; merged under the rule as
changed that night.

What it did: `bestProspect` takes the seeker and qualifies a place only if it can hold
the settlers (a band itself; a settlement's survey the colonists a split would send, a
third when 50 or more). The world-start floor is `START_SITE_MIN_K`, left for order 21.
Contact is awareness: two settlements are in contact when either lies within the other's
range; the network grows by a `Due::ContactGrows` event on the day a range reaches the
nearest unmet settlement, and is rebuilt, not saved, on load. A failing band merges into
the nearest settlement within its own range. A prospect inside the movers' own claim, or
the band's own cell, is not a move. Arrival is the target's cell. Settlements step at
most 5 days and bands 2, with no cap on the count.

Done when:

1. All builds exit 0, C4996 only.
2. `grep -rn "MIN_SETTLEMENT_K\|CONTACT_KM" src` finds nothing; of the literals, only
   `src/population.h:230`, the world-start 80 km spacing, left for order 21.
3. Neither cap ever bound, pre-order, 700 years: settlements 0 of 11,002,345 and 0 of
   10,398,542 (seed 7), 0 of 12,509,453 and 0 of 12,466,335 (seed 3); bands 0 of
   2,008,148, 2,059,590, 2,359,493, 2,486,477.
4. Year 700 on the branch, cold / heat, before -> after: seed 7 settlements 8123/8207 ->
   8770/8206, people 1,985,115/1,999,743 -> 2,531,832/2,011,739, farming 163/207 ->
   2725/65, raids 442,100/451,298 -> 398,447/412,307; seed 3 settlements 8913/9178 ->
   9900/10232, people 2,071,535/2,169,828 -> 3,099,370/3,726,975, farming 81/334 ->
   4469/5741. People at least 70% of before everywhere (101-172%). On `nightly`, against
   `nightly` before the merge: seed 7 people 1,892,664/1,959,246 -> 3,814,365/3,755,098,
   settlements 9883/9451; seed 3 people 1,960,357/2,104,721 -> 2,412,134/3,096,460,
   settlements 9433/9976.
5. Wall clock, branch, pre and post side by side under load: seed 7 1074.8 -> 1687.3 s
   and 1110.4 -> 1473.2 s, seed 3 717.7 -> 863.5 s and 738.2 -> 982.0 s (at most 1.57x).
   On `nightly`, whole runs: seed 7 577 -> 725 s, seed 3 687 -> 865 s (1.26x).

Files beyond the list: `src/savefile.h`, `src/main.cpp`, `Technical/Globe Viewer.md`,
`Design/Conflict.md`, `Design/Technology.md`.

Unsure of: the survey's settler count (a third of a settlement of 50 or more). A band's
own ground is its cell. Each new contact redraws every technology clock on both sides;
on `nightly` before 19, husbandry was invented on seed 7 in year 341 on the 10,000-year
serendipity clock, which never happened on `main`: whether redrawing changes the odds
is worth a look. Neighbour lists reach about 450 km for old settlements. For the game:
the awareness and teachers lines, arrivals in the target cell.

## Review

**Keep**, 2026-09-29. Merged into `main` (`5ca4701`), with the whole night, on the developer's acceptance. Open: whether each new contact redrawing every technology clock changes the odds of invention (husbandry appeared on seed 7 before 19). Still to see in the game: the awareness and teachers lines, arrivals.

Review note for the night: [[Dev Log/Nightly/2026-09-28]].
