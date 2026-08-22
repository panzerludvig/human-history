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

---

## 2026-08-22 — First Core Concepts

### What was done
Promoted three concepts to Core: [[Design/Map-Centric]], [[Design/Event-Driven]], [[Design/Spherical World]]. Each got a fresh note written from scratch rather than copied from Delegate.

### Decisions and reasoning

**Foundational concepts decided before the first increment**
The promotion procedure in [[Design/Core Concepts]] says a concept enters Core only if a desired end state needs it. These three were admitted without that step because they are constraints on the *shape* of every mechanic, not mechanics themselves — choosing them later would mean rewriting whatever came first. The procedure still applies to everything else.

**Event-driven stated as a prohibition on polling, with two sanctioned alternatives**
Compute-the-moment (schedule an event at a calculated time, invalidate on state change) is preferred; trigger-on-manipulation (check a condition only when a dependency is touched) is the fallback. Collision is the worked example. Writing it as "never poll" rather than "prefer events" makes violations obvious.

**Spherical and Earth-like**
Earth-like is a default for scale and intuition, not a requirement to model Earth. Coordinate representation deliberately left open.

---

## 2026-08-22 — Version 1: Globe Viewer

### What was done
Built and ran the first executable: a Win32/OpenGL window showing a procedural Earth-like globe with wheel zoom (10 km across the screen up to one full side of the globe) and left-drag pan. Documented in [[Technical/Globe Viewer]]; stack recorded in [[Technical/Architecture]].

### Decisions and reasoning

**C++ with no dependencies**
The machine had MSVC but no CMake or package manager, and the VS C++ workload had to be installed during the session. Raw Win32 + hand-loaded OpenGL avoids a dependency decision before there is anything to depend on. Revisit when input/UI needs grow.

**Raycast sphere, procedural terrain in the fragment shader**
A mesh-and-texture globe needs a tiling/LOD system to reach 10 km; a raycast sphere with a noise function of the surface normal reaches any zoom with one code path. The cost is that terrain has no CPU representation yet — flagged as an open question rather than solved now, per [[Meta/Incremental Design]].

**Integer hash for noise**
A sin-based hash visibly breaks at the coordinate magnitudes reached at max zoom. pcg3d on integer lattice coordinates is stable everywhere.

**Sun fixed to the camera**
A world-fixed sun leaves half the globe dark at the zoomed-out view, which is useless for a map. Lighting is for legibility, not realism.

**Tuning done by screenshot**
Land fraction, snow extent, and relief visibility were each judged from captured views at several positions and zooms rather than guessed. The starting bias put almost a whole hemisphere underwater; snow originally covered mid-latitude lowlands; relief was invisible until the sun was moved off the camera axis.

---

## 2026-08-22 — Menus, Worlds, Save/Load

### What was done
Added a main menu (New World / Load World / Quit) and an Esc pause menu (Save World / Main Menu / Quit Game). Introduced the notion of a world as a seed plus camera state, saved as a text file.

### Decisions and reasoning

**Native Win32 controls for menus**
Text rendering in OpenGL without libraries means building a font atlas; not worth it for six buttons. Win32 child controls over the GL surface give working, accessible menus now. They will look out of place once the game has a style, and the map-centric principle says menus should be rare anyway — so this is scaffolding, not UI direction.

**World = seed**
Because terrain is a pure function, a world needs no stored data beyond its seed; the seed rotates and offsets the noise field. The rotation is the important part: an offset alone must stay small for float precision and would give only mildly different worlds. This keeps save files trivial now, but note that as soon as the world has mutable state (anything the simulation changes), saves become real serialization — see [[Technical/Architecture]].

**Plain-text save format**
Key-value lines, no versioning yet. Readable and diffable; will need a version field the moment a second kind of data is added.

---

## 2026-08-22 — World Generation Options

### What was done
New World now opens a generation screen: editable seed (with Random), land %, and concentration %. Saves gained a `version` line and the two new parameters.

### Decisions and reasoning

**Land % enforced by quantile, not by bias**
A fixed bias on the noise gives a land fraction that drifts with every shape change. Instead the continent field was ported to C++ (`src/terrain.h`, bit-for-bit the same hash and constants), the sphere is sampled at world creation, and the sea level is the quantile that yields the requested fraction. Cost: ~40k field evaluations once per world; benefit: the number in the box is the number on the globe, for any concentration.

**Concentration as one parameter driving three knobs**
Frequency, domain-warp strength, and a blend toward a "ridge web" field (land along the zero set of a second noise). High concentration = low frequency and little warp, so the quantile picks one big blob. Low concentration = high frequency, heavy warp, and the web, which thresholds to thin strips and chains. One slider that reads as "how clumped is the land" rather than three that need explaining.

**First CPU terrain**
This is the first piece of terrain the simulation could query. It was done for the land-% feature, not as the terrain-architecture decision — that question stays open in [[Technical/Architecture]], but the mirrored-function approach now has a working example to judge.

---

## 2026-08-22 — Heights in Metres, Lakes and Rivers

### What was done
Terrain height is now in metres, fully mirrored on the CPU. A hydrology pass at world creation finds lakes (priority flood) and rivers (flow accumulation) on a 2048×1024 grid and hands them to the shader as a texture. Lakes render as flat surfaces whose shoreline the GPU decides; rivers render as lines between cell centres with noise displacement.

### Decisions and reasoning

**Keep the function as the source of truth; hydrology is a derived layer**
The user wants the map to stay a function. Lakes and rivers are non-local and cannot come from a point function, so they are computed from it and fed back in — the function plus sparse data. This is the pattern that terrain modification will also use, so hydrology doubles as the prototype of the overlay mechanism.

**Metres before hydrology**
Lake levels, river widths and (later) tides all need real units. The unitless noise was scaled by a single constant so nothing else changed.

**Equirectangular grid, not geodesic**
Simplest possible neighbourhood (8 neighbours, longitude wraps) and trivial GPU lookup. Polar cells are tiny and distorted, but the poles are ice. A geodesic grid can replace it if the simulation grid needs uniform cells; the shader only depends on a cell lookup and a centre function.

**Rivers drawn by the GPU from segments, not rasterised**
Rasterising rivers into a texture at sub-cell resolution would need a far larger texture. Instead each pixel tests nearby cell-centre segments, which gives exact sub-pixel lines at any zoom and lets noise displacement bend them. Straight 8-direction runs were visible at continental scale until a second, ~25 km displacement was added.

**Nothing saved**
The layer rebuilds from the seed in well under a second, so the save format is unchanged.

---

## 2026-08-22 — Lake and River Distribution

### What was done
Tuned hydrology toward an Earth-like distribution: few very large lakes and rivers, more mid-sized, many tiny ones.

### Decisions and reasoning

**Basin breaching by seeded roll rather than erosion simulation**
Priority flood fills every basin to its spill point, which on unweathered noise terrain produces inland seas and hundreds of 400 km² mountain lakes. Real basins are mostly breached. Simulating that erosion is a large project; a per-basin seeded roll (70% drain, 27% shallow lake capped at 60 m, 3% full great lake) gives the right *distribution* immediately and is deterministic from the seed. If erosion is ever simulated, it replaces this roll.

**River visibility scales with zoom**
The number of big rivers was never wrong — drainage area is physical. The problem was a 1 px width floor that made every river above threshold look the same from orbit. Now the drainage threshold for drawing rises with km-per-pixel, so the orbit view shows only continental rivers and mid-sized ones appear as you zoom. This is a rendering rule, not a data change.

**Ponds from the function, not the grid**
Tiny lakes are below any practical grid. They are generated in the shader from a fine noise gated by flatness and moisture. This is the first "painted" water — it has no hydrology — and is acceptable because the user explicitly does not need the tiniest streams modelled.

---

## 2026-08-22 — Tectonic Plates

### What was done
Added tectonic plates as the first generation step. Plates drive crust (where continents tend to be) and uplift (where mountain ranges are, and their direction). The height function samples the plate layer on both CPU and GPU; hydrology runs after, so rivers and lakes adapt to the ranges automatically.

### Decisions and reasoning

**Plates are a layer the function samples, not a replacement for the function**
The user wants the map to stay a function. Plates are global information, so they are computed once per world into a small texture (1024×512), and `height(p)` reads uplift, crust and belt direction at p. The noise continents remain; crust only biases them, so the land % quantile and the concentration parameter still work as before.

**Physical boundary types rather than a generic "mountain belt"**
Convergent, divergent and transform boundaries with continental/oceanic asymmetry give Andes-style coastal ranges, Himalaya-style interior belts, island arcs, trenches, mid-ocean ridges and rifts from one mechanism. The first version had a discontinuity at subduction boundaries (uplift on one side, trench on the other, meeting at zero distance) which rendered as a cliff wall along the coast; profiles were rewritten as continuous functions of signed distance.

**Anisotropy only on coarse octaves**
Stretching all octaves of the range noise along the belt produced hair-like parallel lines at close zoom. Now only the first three octaves are stretched (the range's shape); finer octaves are isotropic (the peaks).

**Rock colour from physical slope and altitude**
The relief shading uses an exaggerated per-pixel slope that is zoom-dependent, so using it for rock colouring made mountains look smooth from far away. Rock now comes from true rise-over-run plus an altitude tint above ~2000 m, so ranges read as mountains at every zoom.

**Debug overlay**
A plate overlay (P key) was essential for judging whether the boundary logic was right; kept as a permanent debug feature.

**Second-nearest-plate artifact**
A second cliff survived the profile fix: a perfectly straight wall inside a plate. Cause: crust and uplift were computed from the *second-nearest* plate, whose identity switches abruptly along the bisector between two neighbours. Replaced with a sum over all plates weighted by distance to each pairwise boundary, which is continuous everywhere. Worth remembering: any Voronoi-derived field that uses "the second nearest" has this seam.
