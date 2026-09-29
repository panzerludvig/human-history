# Work Orders

How changes to the game move from an idea to code on `main`: shaped during the day, implemented unattended at night, reviewed in the morning. The orders live in `work_orders/` at the repo root, one folder per stage; the file format and the rules a run follows are in `work_orders/README.md`. This note is the why and the rules.

---

## The day

Building the game has three phases, and the developer's time goes to two of them:

1. **Shaping and planning.** Looking at the state of the game, evaluating what last night produced, deciding what to add or change next, and writing it down as a work order.
2. **Implementation.** Unattended, at night, by a coding agent working through the planned orders.
3. **Review and rework.** In the morning: keep, revert, or rework each night's result, then plan the next batch.

The point is that daytime is never spent waiting on implementation. An order is written when the thinking is done; the code is written while nobody is watching; the judgement happens the next morning with the result in hand.

---

## Four stages

An order is in one of four folders, and the folder is its stage:

- **Ideas** (`work_orders/ideas/`). A problem worth building something for, not yet ready for a run. Anything may go in, and an idea can sit for as long as it needs; this is where an order is shaped. A suggestion Claude wrote in [[Meta/Suggestions]] becomes an idea only when the developer promotes it, so the ideas are always ones the developer chose.
- **Planned** (`planned/`). Ready: a run can finish it without asking anyone. Only planned orders are picked up at night, which is what makes the folder the night's work list.
- **Nightly** (`nightly/`). Taken by a run, and waiting for the morning. Everything the night touched is here, passed or failed, so the morning reads one folder.
- **Implemented** (`implemented/`). Kept and merged into `main`.

The stages replace a single queue in which every order carried a status and all of them sat together (2026-09-15 to 2026-09-27). With one bucket, whether an order was ready was a word in its first line, and after one night the queue held seven orders none of which could run. Folders make readiness visible at a glance and make the run's rule trivial: take `planned/`.

The four stages are the forward path. An order also goes back, and the morning decides which way:

- **Back to planned** when the order was deferred (it conflicted with another taken the same night) or skipped, or when the plan was sound and the code was not: a build or probe that failed for a reason the order can name.
- **Back to ideas** when the night showed the plan itself was wrong.
- **Dropped**, deleted with a line in the night's review note saying why. Git keeps the file; the review note is the record that it existed.

---

## What makes an order planned

Planning and implementing settle different kinds of decision. Planning settles **what**: the outcome wanted and every design decision it rests on, the choices a player would notice or that change what the simulation does. Implementing settles **how**: which functions to write, where a value lives, in what order to change things, and which orders share a night. An order is planned when nothing of the first kind is left open, whatever of the second kind remains. A run that meets an open design question would have to guess, and a guess made at night is reviewed as if it were a decision; an order that prescribes the code takes away the judgement the implementer is there to use (2026-09-28).

Concretely:

- **The design exists.** A game addition points at its design note in `Design/`, with status at least Designed ([[Meta/Status Vocabulary]]). A refactoring points at the standard or Technical note it serves. `Codex.md`'s rule applies: if it is not written down, it does not exist, and an order cannot implement it.
- **The outcome leaves no design decision open.** It states what must be true afterwards and the decisions behind it; an approach that shaped the order may appear, marked as a suggestion.
- **The finish line is machine-checkable.** "Done when" names a build and a probe run, and the numbers or image the probe must produce. The developer verifies in-game in the morning; the run cannot, so it verifies by probe (`standards/general.md` §Verification).
- **Dependencies are real, and built.** "Depends on" lists the orders whose result this one needs, and an order runs only once they are on `main`. It also becomes planned only once they are on `main`: an order shaped on top of something not yet built is shaped against a plan, and plans change (2026-09-28). An idea that waits says so in its Status line ("waits for NN") and stays at its problem and open questions until what it waits for is implemented; the flag keeps it from being taken up too early without shaping work that may not survive. Two orders touching the same file is not a dependency and does not keep either out of planned: the order's Files list is a guide for the run's scheduling, not a claim on the files.
- **The evidence is current.** Line references rot with every merge; they are checked on the day the order moves to planned.

---

## The night

A run chooses the night's orders from `planned/`. The number is the priority the developer chose and the run follows it; what it decides is which orders fit the night together. Only independent orders share a night: of two that are not, the higher-priority one is taken and the other waits, and an order whose dependency is not yet on `main` waits too. Orders left out are named in the review note with the reason. The run moves each order it takes to `nightly/`, and skips only what it cannot do, writing down why.

Branches:

- `nightly/YYYY-MM-DD` is forked from `main` when the run starts, into its own worktree. It is the integration branch for that night.
- Each order gets `wo/NN-slug`, forked from `main`, so an order branch shows exactly one order's diff against `main` and can be judged, merged or reverted alone. Orders are never stacked on each other.
- When an order's "Done when" passes on its own branch, the branch is merged into `nightly`, and the probe runs again there, against everything merged before it. Passing alone and failing after the merge is the interaction case: an addition that changes the world for every other addition. That result is recorded, not hidden.
- If the run misjudged and two orders conflict when the second is merged into `nightly`, the conflict is not resolved by editing: the merge is aborted and the later order is deferred, to be taken again from `main` on another night. A workaround written at night to make two changes fit would be a design decision nobody made (2026-09-28). The one exception is a conflict that is only textual: both sides add lines next to each other -- two paragraphs appended to the same Technical note, two entries in the same list -- and keeping both, unedited, is the whole resolution. The standards make that case common, since every order updates its Technical note in the same commit. The run resolves it, runs both orders' probes, and names the files in both Run sections.

Fail loud, per order. A build that breaks or a probe that misses marks the order failed with the output, leaves its branch, and the run moves to the next order. `nightly` only ever holds orders that passed on it. `main` is never touched.

Each order is implemented in its own subagent, launched with the description `Implement work order NN`. That keeps one order's work out of the next one's context, and it is what lets the night's token use be counted per order: Claude Code keeps a transcript per subagent, and `tools/night_tokens.py` sums each one.

The launch is the standing instruction to commit, which `standards/agent-use.md` otherwise forbids. The run commits on order branches and merges into `nightly`, and nothing else. The deviation is written in `work_orders/README.md` next to the rule.

**The night works in worktrees** (2026-09-28). The nightly branch gets its own git worktree under `.claude/worktrees/`, and each order's subagent works in a worktree of its own; the main working copy is never switched to another branch, written to or built in. Planning goes on in the main copy while a night runs, and nothing the night does can land on what the day is editing, or the other way round. The game built in the main copy's `build\` keeps running, since each worktree builds into its own. The dot folder keeps the worktrees' copies of every note out of the Obsidian vault. From the launch to the morning the orders in `planned/` are frozen, since the run implements them as they read at launch; everything else is edited and committed on `main` as usual, and the morning merge brings the two together.

Each order ends the night with a **Run** section appended to its file, and the night ends with its **review note**.

---

## The review note

Every night gets one note in the vault, `Dev Log/Nightly/YYYY-MM-DD.md`, so the developer reads the night in Obsidian rather than across order files and branches. The run writes it, since only the run has everything at hand, and the morning completes it:

- a **summary** of what the night changed and the orders it included, with their outcome;
- **one section per order**: what it changed, the probe result, what the run was unsure of, and the morning's verdict with its reason;
- the **token use**, per order, for the run's own overhead, and in total, from `tools/night_tokens.py`. Output, input, cache writes and cache reads are reported apart. Cache reads are most of the count and cost least, so a single total would say little about what a night cost; the split shows which order was expensive and why;
- what the **morning** did: the merge into `main` or why not, the branches deleted.

The review note is the night's Dev Log entry. The reasons behind each keep and revert are the why that [[Dev Log/Log]] exists to keep, so they are written once, there, and `Log.md` gets one line linking to the note.

---

## The morning

The developer reads the review note and the nightly branch, and decides per order:

- **Keep.** The order stays merged in `nightly` and moves to implemented with the merge.
- **Revert.** The merge commit is reverted on `nightly`, or the order branch is dropped before it reaches `main`. The order goes back to planned or ideas, with its Status line saying why, or is dropped.
- **Rework.** The order goes back to ideas with what was wrong, and is shaped again.

Then `nightly` is merged into `main`, or not. That merge is the developer's deliberate act, as [[Meta/Git]] already says for every merge; the run never does it. Once merged, the night's worktrees are removed, its branches deleted, and the kept orders move to `implemented/`.

This morning pass is also the recurring check of `planned/`: the line references there are checked against the tree, since the night's merge has just moved them.

---

## Relation to the branch rule

[[Meta/Git]] allows one working branch and side branches only for experiments that are merged or written down before the working branch moves on. Order branches and the nightly branch are that kind of side branch: each one is merged or recorded in its order's Run section before the next night starts, and none outlives its morning review unmerged without a line in the branch map. The lesson behind the rule, five days built on a replaced grid, is why the morning pass exists at all.

---

## What is still provisional

The process was written on 2026-09-15 before any night had run (work order 10), and given its stages on 2026-09-27 after the first night. What the next nights show decides the open points: whether one order per branch is the right unit, how much a Run section needs to say, and whether the second probe run on `nightly` is worth its time. Those answers go into `work_orders/README.md` and the reasoning into [[Dev Log/Log]].
