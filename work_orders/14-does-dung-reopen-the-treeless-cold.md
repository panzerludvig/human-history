# 14 — Measure whether dung reopens the treeless cold

**Status:** open (2026-09-18)

## Problem

[[Design/Resources]] makes a prediction that has never been observed. Heat is a need with
several production modes, and dung is the one that is supposed to keep treeless country
habitable: "When husbandry spreads, the treeless cold should reopen — that is the dung
mode's claim, still to be observed."

It was not observed because husbandry is barely invented inside the measured horizon: the
serendipity clock's mean is 10,000 years, and the run is 700. So the 700-year measurement
shows treeless population **falling** — 33,632 to 14,705 people, the largest single effect
the heat need had — with `herd/person 0.00` on that ground. Either the dung mode works and
the horizon is too short to show it, or the constant is too small to matter and the model
quietly makes the steppe uninhabitable for good. Nothing in the repo distinguishes those.

This is a claim in a design note at status Implemented with no evidence behind it, which
`Codex.md` treats as a thing that does not exist.

## Evidence

- `Design/Resources.md` §Built and measured — the claim, and the 700-year numbers that do
  not test it.
- `src/settlement.h:388` — `DUNG_KG_PER_FED = 4.0f`, fuel per people-fed unit of herd,
  never exercised on treeless ground in any recorded run.
- `src/population.h:509` — `float dung = s.herd * DUNG_KG_PER_FED;` the whole of the mode.
- `src/test_resources.cpp:49-50` — the probe already reports `herdBare` (herd per person on
  treeless ground); it has printed 0.00 in every run.
- `src/technology.h:15` — `INVENT_MEAN_YEARS = 10000.0`, the serendipity clock husbandry
  draws against; `technology.h:24` `HERD_SEED = 1.0f`.
- Last run quoted in `Design/Resources.md`: treeless 107 settlements / 14,705 people with
  heat, 174 / 33,632 without.

Line references checked against commit `8d974f3` on 2026-09-18.

## Design

[[Design/Resources]] §Needs are abstract; production has modes, at status Implemented. No
code design change is proposed: this order measures what is already built and writes the
answer into the note. If the answer is that the mode is too weak, the constant change is a
**separate** order written in the morning with this one's numbers as its evidence — a run
must not tune a constant to make a prediction come true.

## Recommended change

1. A `--seed-husbandry` switch on `test_resources`: every settlement whose pasture clears
  the suitability gate starts practising husbandry at t=0, exactly as fishing is seeded in
  `technology::init`. This is a probe-only path; the game's invention clock is untouched.
2. Report, for treeless ground (`sWood < 0.15`) in each pass: settlements, people,
  herd per person, the share of the heat need met by dung, by byproduct and by cutting, and
  cold deaths.
3. Run the existing three-way comparison with it: hearths cold, hearths burning, hearths
  burning with husbandry seeded.
4. Write the three columns into `Design/Resources.md` under the existing measurement, and
  state plainly which way the claim came out.

## Files

- `src/test_resources.cpp`
- `Design/Resources.md` (the measurement paragraph)

Touches nothing the game builds, so it shares no file with orders 11-13 and can run on any
night.

## Done when

- `build_testresources.bat` builds; `build\test_resources.exe 7 700` and
  `build\test_resources.exe 7 700 --seed-husbandry` both run.
- The probe prints treeless settlements, treeless people, herd per person and the dung share
  of the heat need for all three passes, and the run quotes the table.
- The claim is answered in the Run section in one sentence: with husbandry seeded, treeless
  population at year 700 is at or above the no-heat baseline's 33,632 (the claim holds), or
  it is not (the claim fails and the morning writes a constants order).

## Depends on

Nothing.
