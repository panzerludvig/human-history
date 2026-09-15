# Agent-use standards — Human History

How the developer and a coding agent (Claude Code or other) work on this repo. The
workflow itself — sessions, Dev Log, suggestions, todos, recurring tasks — is in `Codex.md`;
this file covers only what the standards add.

## Reading

- The standards are read at session start through the `@` imports in `CLAUDE.md`. A
  session that starts without them is started wrong; say so and read them.
- Before changing a system, read its Technical note and the design note the header comment
  names. The code is read after the notes, not instead of them.

## Corrections become rules

- A correction given mid-session ("we don't do X here") that applies beyond the current
  task becomes an edit to these files in the same session, with the reason. A correction
  that lives only in the conversation is lost at the next session.
- A rule that turns out to be wrong is changed or deleted, not worked around. Log the
  reasoning in `Dev Log/Log.md`.

## Ownership

- Agent-written code is reviewed like any other code. The developer who ran the agent is
  the author and owns the result.
- An agent never deletes a branch, force-pushes, rewrites history, or merges without an
  explicit instruction naming the branch. The branch map in `Meta/Git.md` and the lesson
  behind it (five days built on a replaced grid) are the reason.
- An agent never commits without being asked. When asked, it commits what was discussed,
  with the probe output or in-game check that verified it, and nothing else.

## Deviations and suggestions

- A deviation from a standard that the agent wants to make is proposed, with its reason,
  before the code is written. If approved it is written next to the code
  (`general.md` §Deviations).
- Alternatives worth considering but not asked for go to `Meta/Suggestions.md`, as
  `Codex.md` says, not into the code.
