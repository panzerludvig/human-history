# Git

Version control for Human History. The repository is hosted at https://github.com/panzerludvig/human-history.

---

## Workflow

Commit directly to `main` while the project is notes-only. Commit at the end of every session at minimum, and after any decision that is written down. Introduce branching when there is code to protect.

---

## Commit Messages

- Imperative mood, short subject line (e.g. `Add core concepts stub`, `Decide on turn structure`).
- Body only when the subject can't carry the why — and the why should usually be in [[Dev Log/Log]] instead.

---

## Branching

**One working branch at a time**, named in this section. Everything new goes on it. A side branch exists only for an experiment that might be thrown away, and it is either merged or written down before the working branch moves on.

**The rule, and why.** Before starting work on a branch other than the working one, or forking a new working branch, list every branch that is ahead of `main` and say in [[Meta/Open Threads]] what each holds and whether it is merged. The geodesic grid was built on `climate-wind` on 2026-09-02, the working line forked the day before, nothing in the vault mentioned the mesh, and five days of atmosphere work were then built on the grid the mesh had replaced ([[Technical/Geodesic Grid]]). Work that is not on the working branch and not in the vault does not exist.

**Order and nightly branches.** A night run ([[Meta/Work Orders]]) creates `nightly/YYYY-MM-DD` from `main` and one `wo/NN-slug` branch per work order from the same base. They are side branches in the sense above, with their record kept in the order files rather than here: each order's Run section says what its branch holds and whether it reached `nightly`, and the morning review either merges `nightly` into `main` or reverts and drops. The run never merges into `main`; that is the developer's act. A night's branches are deleted once the morning review is done, so any `wo/*` or `nightly/*` branch older than the last review is an unfinished review, not reference code. Reference branches are still listed one per row below.

**Branch map (2026-09-14; `prescribed-climate` merged into `main` this day, fast-forward, 37 commits):**

| Branch | State | What it holds |
|---|---|---|
| `main` | **working branch** | The climate as rules of thumb (2026-09-09), the real-elevation Earth template with Natural Earth's lakes (2026-09-14), and behind their flags the prescribed climate, the sigma core, the lat-lon and geodesic quasi-geostrophic models, the two-layer water and the all-physics mode. |
| `prescribed-climate` | merged into `main` 2026-09-14 | Can be deleted. |
| `conserving-atmosphere` | 2 commits ahead of `main`, unmerged | Sea ice as a mass and the seasonal zonal probe, as they were before the prescribed climate; both were carried into `prescribed-climate` by hand. Reference only. |
| `climate-wind` | 26 commits ahead of `main`, unmerged, forked 2026-09-01 | The prognostic atmosphere of 2026-09-02: the geodesic grid and its port (now copied across), sea ice as an object, cloud from motion, the polar cloud and lapse-rate investigations. The physics is superseded by the prescribed climate; the grid is not. |
