# 18 — Units in the names of state fields; enum classes for closed sets

**Status:** idea (2026-09-28) — split out of order 09; the scope is the open question

## Problem

Core state fields carry no unit in their name, and several closed sets are plain integers.

Violates `standards/general.md` §Names carry units and `standards/cpp.md` §Types and
ownership (`enum class` for closed sets, switched on without a `default`).

## Evidence

- Unit-less names: `Settlement::S, P, R, t, herd, claim[], fuelS`
  (`src/settlement.h:600-652`), `Band::water` (`settlement.h:702`), `Camera::altitude` in
  Earth radii (`camera.h:46`), and nearly all atmosphere state.
- Plain integers for closed sets: `panels::Panel::kind` and `tab` (`panels.h:29, 32`),
  `App::genKind` and `debugMode` (`main.cpp:61, 68`), `news::State::level` (`news.h:41`).
  The event-queue kinds are already `enum class Due` (`sim.h:112`).

Line references from order 09's evidence, checked against `f9c731f`; recheck before
planning.

## Design

_To be filled in._ The question is scope. The settlement fields are read in nearly every
file under `src/`, so renaming them collides with any other order that touches the
population model and would need a night of its own. The enum classes are small and local.
Whether the renames are worth that, or happen only as other orders touch the fields, is the
decision to make.

## Outcome

_To be filled in._

## Files

_To be filled in._

## Done when

_To be filled in._ Probe outputs byte-identical: names change, values do not.

## Depends on

Nothing.
