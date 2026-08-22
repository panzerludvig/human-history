# Dev Log

## 2026-08-22 — Project Setup

### What was done
Created the Iron and Blood vault by salvaging the workflow and structure of Delegate, pushed to https://github.com/panzerludvig/iron-and-blood.

### Decisions and reasoning

**Inherit Delegate's workflow, not its design**
Delegate's process — Codex, Meta notes, Dev Log as a reasoning archive, explicit session boundaries — worked and was kept almost verbatim. Its design content was *not* copied: Delegate accumulated many interdependent concepts before any was tested, and the open-question lists grew faster than they could be resolved. The concepts are listed as candidates in [[Design/Core Concepts]] so they are available when needed, but none is decided. Full accounting in [[Meta/Inherited from Delegate]].

**Incremental Design as a Codex principle**
The one new rule: start with the minimum for a playable loop and expand outward. Written as [[Meta/Incremental Design]] and enforced via a "scope check" recurring task and a status on every design note ([[Meta/Status Vocabulary]]).

**Status vocabulary defined now**
Delegate left this as a todo. Defining it before any design note exists means every note starts with an honest status rather than retrofitting later.

**Git conventions filled in**
Kept minimal — direct to `main`, imperative subjects — since the repository is notes-only. Branching deferred until there is code.
