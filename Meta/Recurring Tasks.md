# Recurring Tasks

A list of maintenance tasks that should happen periodically rather than as part of any specific feature work. Each task records what to do, how often, and when it was last done.

At the start of a session ("this is a new session") and at the end ("let's wrap up"), Claude should check this list and remind the user of any task whose interval has elapsed since its last-done date. If nothing is due, say so briefly rather than skipping the check silently.

When a task is performed, update its **Last Done** date and note the outcome in [[Dev Log/Log]] if anything meaningful changed.

---

| Task | Description | Interval | Last Done |
|------|-------------|----------|-----------|
| Quick notes review | Review [[Meta/Quick Notes]] for items to explore or discard | every session | — |
| Suggestions review | Review [[Meta/Suggestions]] for new or unresolved items | every session | — |
| Open threads review | Check [[Meta/Open Threads]] for threads the core now needs | every session | — |
| Standards review | Review Codex standards for accuracy and relevance | 1 month | 2026-08-22 |
| Scope check | Re-read [[Meta/Incremental Design]]; cut anything that has grown beyond the next increment | 2 weeks | 2026-08-22 |
| Optimization review | Look for potential optimizations in code | disabled | — |
| Refactoring review | Check code structure, refactor where it has drifted | disabled | — |
| Documentation audit | Verify notes are up to date and accurate | disabled | — |

Disabled tasks are skipped during the session-boundary check. Re-enable by setting an interval once there's enough project to make them worthwhile.
