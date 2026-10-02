# 18 — Units in the names of state fields; enum classes for closed sets

**Status:** planned (2026-09-28) — split out of order 09; restated 2026-10-02 without line
references

## Problem

Core state fields carry no unit in their name, and several closed sets are plain integers
with their meaning in a comment. Some of the names invite a wrong reading: a herd "of 100"
is not 100 animals, and a camera altitude of 0.1 is 637 km.

Violates `standards/general.md` §Names carry units ("a quantity's name ends in its unit")
and `standards/cpp.md` §Types and ownership (`enum class` for closed sets, switched on
without a `default`).

## Evidence

- Settlement state (`population::Settlement`): `P` people (used across most of the
  population model, the same name on bands and on the per-step state), `S` the food store
  in person-days of rations, `R` land condition 0..1, `t` the sim day `P` and `R` hold at,
  `herd` in people-fed units, `fuelS` fuel in kg, `claim[]` territory reach per sector in
  km. The per-step state (`population::Step`) mirrors them.
- `Band::P`, and `Band::water` in person-days of water.
- `camera::Camera::altitude`, in Earth radii.
- Plain integers for closed sets: `panels::Panel::kind` and `tab`, `App::genKind` and
  `debugMode` in `main.cpp`, `news::State::level` and `kind`. The event-queue kinds are
  already `enum class Due` (`src/sim.h`).
- Saves write these fields by position, not by name (`savefile.h`), so renaming them does
  not touch the save format.

## Design

`standards/general.md` §Names carry units, `standards/cpp.md` §Types and ownership.
Decided 2026-09-28:

- **Rename all of them**: the settlement, step and band fields above, and the camera's
  altitude. Not the atmosphere's state, which is to be redone on the geodesic mesh and gets
  unit-bearing names there.
- **The herd is counted in head.** For now one head feeds one person, so nothing changes in
  value; the herd constants (`DUNG_KG_PER_FED` and the rest) are restated per head with the
  same values. Real head counts and kinds of livestock are order 26.
- The enum classes are part of this order.

## Outcome

- Every field listed above has a name ending in its unit (or saying what it is where it has
  none, as for a 0..1 condition), chosen by the implementer and consistent across
  settlement, step and band. The herd's name and comment say head, with one head feeding
  one person for now.
- The camera's altitude is named for its unit, or held in km.
- `panels` kind and tab, the generation kind, the debug mode, and the news level and kind
  are `enum class`, and every `switch` over them has no `default`.
- Behaviour, saves and the picture are unchanged.

## Files

A guide, not a limit: `src/settlement.h`, `src/population.h`, `src/bands.h`,
`src/claims.h`, `src/farmland.h`, `src/raids.h`, `src/technology.h`, `src/sim.h`,
`src/inspect.h`, `src/overlay.h`, `src/panels.h`, `src/news.h`, `src/menus.h`,
`src/camera.h`, `src/anchor.h`, `src/main.cpp`, `src/savefile.h`, `src/world.h`, the
probes that read these fields, and the design and Technical notes that name them.

## Done when

1. `build.bat` and every `build_*.bat` exit 0 with no warning beyond the existing C4996
   set.
2. `grep -nE "\b(float|double) (P|S|R|t|herd|fuelS|water|altitude)\b" src/settlement.h src/camera.h src/population.h`
   finds none of the old declarations; the Run section lists the old name and the new one
   for each field.
3. Probe outputs byte-identical to the pre-order run: `build\sweep.exe 7 0 rules 1`,
   `build\test_resources.exe 7 40` and `3 40`, `build\test_savefile.exe 7 3`.
4. A save written before this order loads, and game screenshots (with `HH_BENCH=5`) at the
   five views of order 06's Done when are byte-identical.

## Depends on

Nothing. It touches nearly every file of the population model, so it shares a night with
no order that does.
