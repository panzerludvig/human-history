# Work Orders

How planned changes to the game are queued during the day, implemented unattended at night, and reviewed in the morning. The queue itself lives in `work_orders/` at the repo root; its file format is in `work_orders/README.md`. This note is the why and the rules.

---

## The day

Building the game has three phases, and the developer's time goes to two of them:

1. **Shaping and planning.** Looking at the state of the game, evaluating what last night produced, deciding what to add or change next, and writing it down as a work order.
2. **Implementation.** Unattended, at night, by a coding agent walking the queue.
3. **Review and rework.** In the morning: keep, revert, or rework each night's result, then plan the next batch.

The point is that daytime is never spent waiting on implementation. An order is written when the thinking is done; the code is written while nobody is watching; the judgement happens the next morning with the result in hand.

---

## What makes an order queueable

An order can be picked up at night only if a run can finish it without asking anyone. Concretely:

- **The design exists.** A game addition points at its design note in `Design/`, with status at least Designed ([[Meta/Status Vocabulary]]). A refactoring points at the standard or Technical note it serves. `Codex.md`'s rule applies: if it is not written down, it does not exist, and an order cannot implement it.
- **The finish line is machine-checkable.** "Done when" names a build and a probe run, and the numbers or image the probe must produce. The developer verifies in-game in the morning; the run cannot, so it verifies by probe (`standards/general.md` §Verification).
- **The footprint is declared.** The order lists the files it expects to touch. Two orders in the same night sharing a file is a planning smell to fix during the day, not a merge problem to discover at night.
- **Dependencies are named.** "Depends on" lists the orders that must be merged first. The run skips an order whose dependencies are not on the nightly branch.

An order that fails any of these stays `open`; it becomes `queued` when they all hold.

---

## The night

A run is launched with the queue in the order the numbers give. The number is the position the developer chose, and the run does not reorder to fit more in. It walks the list from the top, takes each queued order in turn, and skips only what it cannot do, writing down why.

Branches:

- `nightly/YYYY-MM-DD` is forked from `main` when the run starts. It is the integration branch for that night.
- Each order gets `wo/NN-slug`, forked from the same base as the nightly branch, so an order branch shows exactly one order's diff against `main` and can be judged alone.
- When an order's "Done when" passes on its own branch, the branch is merged into `nightly`, and the probe runs again there, against everything merged before it. Passing alone and failing after the merge is the interaction case: an addition that changes the world for every other addition. That result is recorded, not hidden.
- A merge that does not apply cleanly is a failed order. The branch is left as it is for the morning. Nothing is force-resolved at night.

Fail loud, per order. A build that breaks or a probe that misses marks the order `failed` with the output, leaves its branch, and the run moves to the next order. `nightly` only ever holds orders that passed on it. `main` is never touched.

The launch is the standing instruction to commit, which `standards/agent-use.md` otherwise forbids. The run commits on order branches and merges into `nightly`, and nothing else. The deviation is written in `work_orders/README.md` next to the rule.

Each order ends the night with a **Run** section appended to its file: `done`, `failed`, or `skipped`; the branch; the commit or merge hash; the probe output quoted; and anything the run was unsure of.

---

## The morning

The developer reads the Run sections and the nightly branch, and decides per order:

- **Keep.** The order stays merged in `nightly`.
- **Revert.** The merge commit is reverted on `nightly`, or the order branch is dropped before it reaches `main`. The order goes back to `open` with a note saying why, or to `dropped`.
- **Rework.** A new order is written with what was wrong; the old one is `done` or `dropped` as the case may be.

Then `nightly` is merged into `main`, or not. That merge is the developer's deliberate act, as [[Meta/Git]] already says for every merge; the run never does it. Once merged, the night's order branches can be deleted, the closed orders move to `work_orders/done/`, and the Dev Log gets one entry for the night with the why behind each keep and revert.

This morning pass is the recurring review of the queue. Line references in open orders are checked against the tree then, since they rot with every merge.

---

## Relation to the branch rule

[[Meta/Git]] allows one working branch and side branches only for experiments that are merged or written down before the working branch moves on. Order branches and the nightly branch are that kind of side branch: each one is merged or recorded in its order's Run section before the next night starts, and none outlives its morning review unmerged without a line in the branch map. The lesson behind the rule, five days built on a replaced grid, is why the morning pass exists at all.

---

## What is still provisional

The process was written on 2026-09-15 before any night had run (work order 10). The first two or three nights use the refactoring orders 01-09 and a plain launcher script. What those mornings show decides the open points: whether one order per night is the right unit or several, how much a Run section needs to say, and whether the second probe run on `nightly` is worth its time. Those answers go into `work_orders/README.md` and the reasoning into [[Dev Log/Log]].
