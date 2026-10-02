# Work orders

One file per piece of planned work, moving through four stages from an idea to code on
`main`. The process, and why it is shaped this way, is in `Meta/Work Orders.md`; this file
is the format and the rules a night run follows.

## Stages

Each stage is a folder, and an order is in exactly one of them. It moves by `git mv`, in a
commit that says why.

- **`ideas/`**: a described problem worth building something for, not yet ready for a run.
  Anything can go here. It is where an order is shaped.
- **`planned/`**: passes the planning checklist below, so a run can finish it without
  asking anyone. Only planned orders are picked up at night.
- **`nightly/`**: taken by a night run. It stays here, whatever the outcome, until the
  morning review.
- **`implemented/`**: kept in the review and merged into `main`.

An order leaves `nightly/` in the morning in one of four directions:

- to `implemented/`, kept, when the night is merged into `main`;
- back to `planned/`, when the order was deferred or skipped, or when the plan was sound
  and the code was not (a failed build or probe with a cause the order can name);
- back to `ideas/`, when the night showed the plan itself was wrong;
- deleted, when it is dropped: the night's review note says why, and git keeps the file.

## Which note an item goes in

- A **todo** (`Meta/Todo.md`) is a task doable in one sitting with no design.
- A **suggestion** (`Meta/Suggestions.md`) is an option Claude raised with no commitment.
  It becomes an idea only when the developer promotes it.
- An **open thread** (`Meta/Open Threads.md`) is a question with no recommended answer.
- A **work order** is a problem with evidence and a recommended change, expected to take
  several commits. It starts in `ideas/`.

## Naming

`NN-slug.md`. The number is the order's id and, in `planned/`, its priority: a run takes
planned orders in numeric order and does not reorder to fit more in. Reprioritise by
renaming. Numbers are never reused.

## Sections

Every order has these, in this order:

- **Status**: one line: the date the order entered its folder, and what it is waiting on
  or how its stage ended (`failed on 2026-10-02: ...`). The folder is the stage; the line
  never contradicts it.
- **Problem**: what is wrong, measured against `standards/` where a rule applies.
- **Evidence**: what shows the problem, named by file, function or behaviour; never by
  line number, which rots with every merge. Where the code is, and how to change it, is
  the implementer's to find.
- **Design**: the design or Technical note the order implements, by vault path. A game
  addition needs its note at status Designed or later; a refactoring names the standard
  it serves.
- **Outcome**: what must be true when the order is done, and every design decision that
  outcome rests on, stated as decided. It says what, not how: which functions to write,
  where a value lives and in what order to change things are the implementer's calls. An
  approach that shaped the order may be given as a suggestion, marked as one.
- **Files**: the files the order is expected to touch, as a guide for the run's
  scheduling. Not binding.
- **Done when**: the build and the probe run, and the numbers or image the probe must
  produce. This is what the run checks, so it has to be checkable without a person.
- **Depends on**: orders whose result this one needs (it builds on their code or their
  behaviour), or none. It is planned, and runs, only once they are on `main`. Sharing files is not a
  dependency; the run keeps orders that collide on different nights. Ideas carry it too,
  and it is how an idea says it must wait: while anything it depends on is not
  implemented, its Status line says "waits for NN", and it is not shaped past its Problem
  and its open questions (no Outcome, no Done when). Plans change and built things turn
  out differently, so an order is shaped against what was built, not what was planned.

Two sections are appended later, never written up front:

- **Run** (by the night run): the outcome (`passed`, `failed`, `deferred` or `skipped`);
  the branch;
  the commit or merge hash; the probe output quoted; anything the run was unsure of.
- **Review** (in the morning): one line with the verdict and a link to the night's review
  note, where the reasons are.

An idea may leave sections as `_To be filled in._`; a planned order may not.

## Planning checklist

Planning settles what is wanted; implementing settles how. An order moves from `ideas/`
to `planned/` when all of these hold:

1. The Design section names a note that exists at the required status.
2. The Outcome leaves no design decision open: anything a player would notice, anything
   that changes what the simulation does, and any choice between rules is decided in the
   order or its design note. What remains open is only how to write the code.
3. Done when names a probe and its expected output, not a judgement. The checks test the
   order's own mechanism. World totals after centuries (settlements, people, farming)
   are quoted for the morning, never gated: whether the world the new model makes is
   good is the developer's judgement, and a single 700-year run moves by more than 10%
   from any change to an early random draw (orders 11 and 12, 2026-09-28).
4. Every order in Depends on is implemented. (Until 2026-09-28 a planned dependency was
   enough; 09, 13 and 15 were planned that way and stand.)
5. The order names no line numbers, and what it says about the code is still true on
   the day it moves.

Overlap with other planned orders is not a criterion. Which orders share a night is the
run's decision, and it only puts independent orders together.

## What the run does

The run works only in worktrees. The main working copy stays on `main` and is never
checked out to another branch, written to or built in, so planning can go on there while a
night is in flight, and the game in its `build\` keeps running. Worktrees live under
`.claude/worktrees/` (a dot folder, which Obsidian does not index), each with its own
`build\`.

The run is launched with `powershell -File tools\night.ps1`, never with `claude` directly.
The launcher runs the whole process tree at BelowNormal priority, so the run uses every
idle core and the developer's work still comes first. No step raises a process's priority.

1. Fork `nightly/YYYY-MM-DD` from `main` into its own worktree,
   `.claude/worktrees/nightly-YYYY-MM-DD`; every step below that touches `nightly` (moving
   orders, merging, the second probes, the review note) is done there. Note the time: it
   is where the token count starts.
2. Choose the night's orders from `planned/`. The numbers are the developer's priority
   and the run follows them. An order is eligible only when everything in its Depends on
   is implemented, that is, already on `main`. Orders are never stacked: of two orders that
   are not independent of each other, only the higher-priority one is taken, and the other
   waits for a later night. The run also leaves an order out when the time box cannot hold
   it. Every order left out is named in the review note with the reason.
3. For each chosen order: `git mv` it to `nightly/`; fork `wo/NN-slug` from `main` (the
   base `nightly` was forked from), so every branch is its own order's change and nothing
   else; implement it in a subagent described exactly `Implement work order NN` (the token
   count finds orders by that description), working in its own worktree for that branch;
   build; run the probe named in Done when.
4. If it passes, merge the order branch into `nightly` and run the probe again there. If
   that passes too, the order `passed`. If the merge conflicts with an order merged
   earlier, the run judges whether the two changes are independent: each still does what
   its order says with the other's lines beside it, and neither needs to know the other
   exists. If it judges them independent it may resolve the conflict, but only with lines
   as written by one order or the other; a resolution never contains a line neither order
   wrote. Two orders adding a paragraph to the same note is the common case; one order
   inserting lines next to a line the other rewrote is another. For each conflicted hunk
   the run writes in both Run sections what each side did and why the two are
   independent, then runs both orders' probes on `nightly`. If the changes are not
   independent, or fitting them together would need a line neither order wrote, the
   merge is aborted and the order is `deferred`, to be taken again from `main` on a later
   night. If a probe fails on `nightly`, the order `failed`. Either way its branch is
   left as is and `nightly` is restored to its state before the merge.
5. If the build breaks or the first probe fails, the order `failed`; the branch is left as
   is.
6. An order that could not be started (its dependency turned out not to be on `main`, or
   the tree would not build before it began) is `skipped`.
7. Append the Run section, then move on. Stop at the end of the chosen orders or the time
   box.
8. Write the night's review note (below), run the token count into it, and commit the
   order files and the note on `nightly`.

`main` is never touched. Nothing is stacked, and no conflict is resolved by writing new
code: an order that is not independent of the rest of the night waits for another night.

**Planning during a night.** From the launch until the morning, the orders in `planned/`
are frozen: the run implements them as they read at launch, and moves them on its own
branch. Changes to one wait for the morning, or go into a new idea. Everything else (ideas,
design notes, new orders) is edited and committed on `main` as usual; the morning merge
brings the two together.

**Deviation from `standards/agent-use.md`** ("an agent never commits without being
asked"): the launch of a run is the instruction to commit. It covers commits on `wo/*`
branches and merges into the night's `nightly/*` branch, and nothing else.

## The night's review note

`Dev Log/Nightly/YYYY-MM-DD.md`, in the vault, one per night. It is the night's Dev Log
entry: `Dev Log/Log.md` gets one line linking to it. The run writes every section but the
verdicts:

- **Summary**: what the night changed, in a few sentences a reader can take in without the
  diffs, and the list of orders with their outcome.
- **One section per order**: what it changed (commits, files beyond its Files list), the
  probe result, what the run was unsure of, and a **Verdict** line left empty for the
  morning.
- **Tokens**: the table from `python tools/night_tokens.py --since <start time>`, one row
  per order, one for the run's own overhead, and the total. Output, input, cache writes
  and cache reads are kept apart: cache reads are most of the count and cost least.
- **Morning**: left empty for the review.

## What the morning does

Read the review note and the nightly branch. Per order, write the Verdict (keep, revert,
rework, drop) and why, and move the file as Stages says. Merge `nightly` into `main` only
by deliberate decision. Then remove the night's worktrees and delete its branches, fill the
Morning section (the merge hash or why not, what was removed), add the line to
`Dev Log/Log.md`, and read the orders in `planned/` against what the night changed:
an order whose problem the night solved or moved is restated or sent back to `ideas/`.

## Provisional

The stages were written on 2026-09-27 after one night had run under the queue that came
before them. The unit of one order per branch, the second probe on `nightly`, and how much a
Run section needs to say are still decided from the next two or three nights, and recorded
here when they are.
