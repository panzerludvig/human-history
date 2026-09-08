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

**Branch map (2026-09-08):**

| Branch | State | What it holds |
|---|---|---|
| `prescribed-climate` | **working branch** | Prescribed climate, the Earth template, the sigma core (`DYN2`, off), the quasi-geostrophic model (`QG2`, off), and now the geodesic grid files. |
| `main` | equal to `climate-rebuild` | Two air layers and the moist convection gate (2026-09-04). Behind the working branch by the whole prescribed climate. |
| `climate-rebuild` | merged (identical to `main`) | Can be deleted. |
| `conserving-atmosphere` | 2 commits ahead of `main`, unmerged | Sea ice as a mass and the seasonal zonal probe, as they were before the prescribed climate; both were carried into `prescribed-climate` by hand. Reference only. |
| `climate-wind` | 26 commits ahead of `main`, unmerged, forked 2026-09-01 | The prognostic atmosphere of 2026-09-02: the geodesic grid and its port (now copied across), sea ice as an object, cloud from motion, the polar cloud and lapse-rate investigations. The physics is superseded by the prescribed climate; the grid is not. |
