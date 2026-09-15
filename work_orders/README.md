# Work orders

One file per piece of planned work, queued during the day and implemented unattended at
night. The process, and why it is shaped this way, is in `Meta/Work Orders.md`; this file
is the format and the rules a run follows.

## Which note an item goes in

- A **todo** (`Meta/Todo.md`) is a task doable in one sitting with no design.
- A **suggestion** (`Meta/Suggestions.md`) is an option raised with no commitment.
- An **open thread** (`Meta/Open Threads.md`) is a question with no recommended answer.
- A **work order** is a problem with evidence and a recommended change, expected to take
  several commits, that a run can finish without asking anyone.

## Naming

`NN-slug.md`. The number is the queue position: the run takes orders in numeric order and
does not reorder to fit more in. Reprioritise by renaming. Numbers are not reused within a
batch; a closed order keeps its number in `done/`.

## Sections

Every order has these, in this order:

- **Status**: one of `open`, `queued`, `in progress`, `done`, `failed`, `skipped`,
  `dropped`, with the date it changed. `queued` means the order passes the checklist
  below; only `queued` orders are picked up.
- **Problem**: what is wrong, measured against `standards/` where a rule applies.
- **Evidence**: file and line references, verified at the date of writing.
- **Design**: the design or Technical note the order implements, by vault path. A game
  addition needs its note at status Designed or later; a refactoring names the standard
  it serves.
- **Recommended change**: the seam, not the rewrite. Steps are listed when there are
  several; each step is a commit.
- **Files**: the files the order expects to touch. Two orders in one night sharing a file
  is fixed during the day.
- **Done when**: the build and the probe run, and the numbers or image the probe must
  produce. This is what the run checks, so it has to be checkable without a person.
- **Depends on**: orders that must be merged into `nightly` first, or none.

Two sections are appended later, never written up front:

- **Run** (by the night run): `done`, `failed`, or `skipped`; the branch; the commit or
  merge hash; the probe output quoted; anything the run was unsure of.
- **Review** (by the developer, in the morning): keep, revert, or rework, and why.

## Queueability checklist

An order moves from `open` to `queued` when all of these hold:

1. The Design section names a note that exists at the required status.
2. Done when names a probe and its expected output, not a judgement.
3. Files is filled in and does not overlap another `queued` order.
4. Every order in Depends on is `done` or is queued ahead of it.

## What the run does

1. Fork `nightly/YYYY-MM-DD` from `main`.
2. For each `queued` order in numeric order: set `in progress`; fork `wo/NN-slug` from
   the same base; implement; build; run the probe named in Done when.
3. If it passes, merge the order branch into `nightly` and run the probe again there. If
   that passes too, the order is `done`. If the merge does not apply cleanly or the
   second probe fails, the order is `failed`, the branch is left as is, and `nightly` is
   restored to its state before the merge.
4. If the build breaks or the first probe fails, the order is `failed`; the branch is
   left as is.
5. An order whose Depends on is not on `nightly` is `skipped`.
6. Append the Run section, then move on. Stop at the end of the list or the time box.

`main` is never touched. Nothing is force-resolved. The run does not choose between orders.

**Deviation from `standards/agent-use.md`** ("an agent never commits without being
asked"): the launch of a run is the instruction to commit. It covers commits on `wo/*`
branches and merges into the night's `nightly/*` branch, and nothing else.

## What the morning does

Read each Run section and the nightly branch. Per order: keep, revert (revert the merge
on `nightly`, or drop the branch), or rework (write a new order). Merge `nightly` into
`main` only by deliberate decision. Then move `done` and `dropped` orders into `done/`
with their Review section filled in, delete the night's branches, write one Dev Log entry
for the night, and check the line references of the orders still open against the tree.

## Provisional

Written 2026-09-15 before any night has run. The unit of one order per branch, the
second probe on `nightly`, and how much a Run section needs to say are decided from the
first two or three nights on orders 01-09, and recorded here when they are.
