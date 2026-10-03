# 31 — Follow a people through time

**Status:** idea (2026-09-29) — the design is open

## Problem

It is too hard to follow what happened to a settlement or a band. A settlement selected
before a long step (a hundred years) may simply be gone afterwards, with no way to tell
whether it moved, joined someone, split, starved on the road or is walking right now.
The model knows most of this at the moment it happens; the game keeps almost none of it.

What the code does today:

- **The panel loses a people on the road.** A community that moves keeps its identity:
  the band carries the settlement's id and the new settlement takes it back
  (`bands.h`, `b.sid = s.id` in the relocation and `s.id = b.sid ? ...` in
  `foundSettlement`). But while it walks, the settlement panel finds no settlement with
  that id and says "This settlement is gone: they picked up and moved on." It does not
  switch to the band carrying them, and says nothing of where they went.
- **The history is only the last step's, and a long step drops most of it.** The event
  log is cleared at the start of every step (`main.cpp` `advanceDays`), and past 250
  events of a kind in one step it stops storing them (`EVENTS_KEPT_PER_KIND`,
  `settlement.h:530`), keeping only the count. A hundred years of a world of thousands
  of settlements passes 250 relocations, splits and raids early, so the selected
  settlement's own events are most likely among those not kept, and its History tab
  says "Nothing happened to them this step."
- **Some links are never written.** Resettling records the new settlement but not the
  band it came from (`EV_SETTLED` and `EV_FOUNDED` pass no band id, `bands.h`
  `foundSettlement`). A band that merges records the settlement it joined and the band,
  but not the people the band came from. A band that dies on the road records nothing:
  `EV_PERISHED` exists (`settlement.h:499`) and nothing ever notes it; the death goes to
  the probe's stderr log only.
- **Departed settlements are erased**, not kept (`sim::sweepDeparted`, "settlements that
  left are erased, not tombstoned"), so nothing remains to ask afterwards.

## Evidence

- `src/panels.h` `content`: the "gone" text for a settlement panel whose id is not found;
  panels hold `sid` or `bandId`, never both.
- `src/inspect.h` `historyText`: filters the current step's `pf.events` by id, eleven
  lines at most.
- `src/main.cpp` `advanceDays`: `app.world.pop.events.clear()` and the counts reset.
- `src/events.h` `note`: the per-kind cap; each event carries `sid`, `sid2`, `bandId`,
  `cell`.
- `src/bands.h`: relocation keeps the id (`b.sid = s.id`); colonists get a new one; the
  `EV_SETTLED`/`EV_FOUNDED` note passes band id 0; the merge note; no `EV_PERISHED`.
- `src/sim.h` `sweepDeparted`.

## Design

_To be filled in._ Open questions:

- **What a people's identity is.** A relocating community keeps its id today and
  colonists get a new one. What of a people that merges into another, or is conquered:
  does its line end, or continue inside the other? Is a split a child of its parent,
  so a people has descendants?
- **What is kept, and for whom.** A short chronicle per people, kept across steps and
  saved: founded, moved (from where to where), split (the child), joined (whom), died on
  the road, lost or took up a technology. For every people, or only for those the player
  watches, with the rest counted as today? What it costs in memory and in the save over
  a thousand-year world of thousands of settlements.
- **What the panel does.** Follows the people through their forms: the settlement
  becomes the band that carries them and the settlement they found again, and says
  where they went when they are gone (joined X, died on the road near Y, founded Z).
  Whether a panel can jump to a relative (the colony, the people they joined).
- **What the map shows.** The selected people's trail across the steps; a marker that
  stays on a watched people through a long step; the camera following on request.
- **Long steps.** Whether a step should stop early when something happens to a watched
  people, so the player sees it happen.
- **The news feed and the log.** Whether the per-step event log stays as it is, for the
  world's news, and the chronicle is separate, or one becomes the other.

## Outcome

_To be filled in._

## Files

_To be filled in._ Likely `src/events.h`, `src/settlement.h`, `src/bands.h`,
`src/sim.h`, `src/savefile.h`, `src/inspect.h`, `src/panels.h`, `src/overlay.h`,
`src/main.cpp`.

## Done when

_To be filled in._ A probe that follows chosen settlements through a long step and
checks that each one's end state (where it is, whom it joined, how it died) is recorded,
and that a relocated people's panel finds it again.

## Depends on

_To be filled in._
