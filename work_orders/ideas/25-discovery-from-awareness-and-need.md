# 25 — Taking up a technology: awareness and need, not only a practising neighbour

**Status:** idea (2026-09-28) — the design is open

## Problem

An aware settlement can start practising a technology only by learning it from a
practising neighbour. Knowing that something is possible does nothing for a people's own
chance of working it out; an aware settlement with no practising neighbour is locked out
until one appears, while an unaware one can still invent it. That is backwards: knowing a
thing can be done makes it easier to find out how, even if it does not hand you the method.

Need is also only one thing, hunger (and the fill cycle for granaries). The model now has
a second need, heat, and a labour budget (order 11): a people that can feed itself but
spends so much of the day getting food that it cannot cut enough wood is short of labour,
and would look for easier ways to get food. That should count as need, less than hunger
but not nothing, and the same pattern should carry the needs still to come.

And the rates are shared: `AWARE_MEAN_YEARS`, `PRACT_MEAN_YEARS`, the two invention clocks.
Each technology should have its own chance of being found and its own modifiers.

## Evidence

- `src/technology.h:226-251` (`redraw`): an aware settlement's rate to practise is
  `suitability * adoptionNeed * sum(neighbour expertise) / PRACT_MEAN_YEARS`; with no
  practising neighbour the rate is zero and the draw parks at infinity.
- `src/technology.h:197-211` (`needWeight`) and `258-292` (`scheduleInvention`): invention
  by need draws only on unaware settlements (`s.tech[tech].aware` returns 0).
- `src/technology.h:219-223` (`adoptionNeed`): hunger through `phi`, or the fill cycle for
  granaries; nothing else.
- `src/technology.h:15-17`: `INVENT_MEAN_YEARS`, `AWARE_MEAN_YEARS`, `PRACT_MEAN_YEARS`,
  shared by every technology.
- [[Design/Technology]] §Discovery and §Spread; the calibration that the farming front
  advances about 1 km a year, as across Neolithic Europe.
- Order 11 (the labour budget), which gives each settlement the free labour after food
  that a labour need would read; order 19 (husbandry from need).

## Design

_To be filled in._ The wishes (2026-09-28) and the questions they raise:

- **Awareness helps you find it yourself.** An aware settlement has its own chance of
  starting to practise, driven by its need, higher than an unaware settlement's chance of
  inventing it. Practising neighbours raise it further. Questions: how much easier
  awareness makes it; whether the neighbours' expertise adds to that chance or multiplies
  it; whether a people that works it out alone starts with the same expertise as one taught
  by a neighbour.
- **Need is more than hunger.** Hunger is the strongest. A labour squeeze counts too: food
  work taking the hours the hearth needs (the heat need unmet because the budget is spent
  on food) makes easier food worth finding, less strongly than hunger. Questions: how needs
  are measured and combined; which needs each technology answers (farming, herding,
  fishing and bows answer food; granaries answer the lean season; future fuel technologies
  would answer heat); the pattern for adding a need later.
- **Each technology its own.** A base chance of discovery, the needs it answers and their
  weights, what makes the ground suitable, how fast it is taught: per technology, in one
  place, instead of shared constants.
- **Calibration.** What stays fixed while this changes: farming first invented about 300
  years into a world, husbandry on about the same schedule (order 19), and the farming
  front near 1 km a year.

## Outcome

_To be filled in._

## Files

_To be filled in._ Likely `src/technology.h`, `src/settlement.h`, `Design/Technology.md`.

## Done when

_To be filled in._

## Depends on

11 (the labour budget the labour need reads). Orders 17 (contact is awareness) and 19
(husbandry from need) change the same rates; this order recalibrates after them.
