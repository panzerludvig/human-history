# 17 — Thresholds derived from the tables they gate

**Status:** idea (2026-09-28) — split out of order 09; each threshold's table to be checked

## Problem

Several thresholds are bare numbers compared against tables whose reach is computable. The
recorded bug of 2026-08-23 was exactly this: a settlement threshold above the maximum
carrying capacity the yield table could produce, so no settlement existed and, because the
clock waited on settlements, time stood still.

Violates `standards/general.md` §Thresholds and clocks ("a threshold derives from the table
it gates").

## Evidence

- `MIN_SETTLEMENT_K = 150` (`src/settlement.h:114`) against a computable maximum K.
- `80.0f` km settlement spacing at `population.h:228` (`population::build`) and
  `bands.h:124` (`bestProspect`), stated only as "~80 km" in a comment (`population.h:203`).
- `CONTACT_KM = 160` (`settlement.h:779`), "twice the minimum settlement spacing", written
  as a number.
- The `20.0f` km arrival radius at `bands.h:422, 432, 504` (`stepBand`).
- `VILLAGE_FIELDS_KM2 = 58` (`settlement.h:345-346`), its derivation in a comment only.
- Sub-step constants at `population.h:683` (`advance`) and `bands.h:216`
  (`integrateBand`).

Line references from order 09's evidence, checked against `f9c731f`; recheck before
planning.

## Design

`standards/general.md` §Thresholds and clocks. No value changes: each threshold keeps its
number and gains an expression or a build-time check.

## Outcome

_To be filled in._ Each threshold above is either written as an expression over the
constants of the table it gates, or carries a `static_assert` that the table can reach it.
Which of the two fits each one is the work of shaping this order: some of these (the 20 km
arrival radius) may have no table behind them at all, and then the order says so.

## Files

_To be filled in._

## Done when

_To be filled in._ Probe outputs byte-identical, since no value changes.

## Depends on

Nothing.
