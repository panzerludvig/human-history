# 10 — Refine the work-order process

**Status:** open (2026-09-15)

## Problem

This directory was created 2026-09-15 with the simplest model that could hold the
refactoring review: one file per item, a fixed set of sections, a number for order. Several
things are undecided and should be decided from experience of using it on orders 01-09
rather than up front.

## Evidence

- `work_orders/README.md` — the current shape.
- `Codex.md` does not yet mention work orders; `Meta/Todo.md` and `Meta/Suggestions.md`
  overlap with them in purpose.
- `Meta/Recurring Tasks.md` has no entry for reviewing open orders.

## Recommended change

Answer these after the first two or three orders have been executed, and write the answers
into `README.md`:

1. **Lifecycle.** Where do done and dropped orders go: deleted, an archive directory, or a
  Dev Log entry only? What must be written down when an order closes?
2. **Boundaries with the Meta notes.** A todo is a task, a suggestion is an option, an open
  thread is an undecided question, a work order is a described problem with a recommended
  change. Write the one-line test for which note a new item goes in, and add a Codex
  entry that says it.
3. **Picking up an order.** Does an order get a plan section appended when work starts,
  or is the plan a Dev Log entry? Who updates the status line, and when?
4. **Granularity.** Orders 02, 04 and 05 each list four to seven steps, each meant to be a
  commit. Is one order per commit better, or one order per outcome with steps inside?
5. **Verification.** Every order here has a "Done when" that names a probe output or a
  screenshot comparison. Should that be mandatory, and should the comparison outputs be
  kept somewhere?
6. **Recurring review.** Add an entry to `Meta/Recurring Tasks.md` for re-reading open
  orders against the current code, since line references rot.
7. **Naming.** Numbered prefixes give order but renumbering on reorder is noisy. Decide
  whether to keep them.

## Done when

- `README.md` answers the seven questions.
- `Codex.md` has a Work Orders entry linking here.
- Line references in orders 01-09 have been checked against the tree once after the first
  refactoring commits.

## Depends on

Experience from any two of 01-09.
