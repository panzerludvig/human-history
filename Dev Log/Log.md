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

**Directional stretch replaced by boundary-distance bands**
The user found concentric stripes and rectangular discontinuities at a mountain belt. Cause: stretching the noise along the belt direction subtracts a component of the *absolute* coordinate (~20 units from the origin), so wherever the direction field rotates — at every junction, and at every cell edge of the 40 km plate texture — the noise coordinates sweep across many cells and draw contours. Any anisotropy expressed in a rotating frame far from its origin has this failure. Replaced with isotropic ridged peaks plus cosine bands of the distance-to-boundary scalar, which is smooth, intrinsic and needs no frame. Belt direction is no longer stored.
The first band version was a pure cosine of distance — perfectly periodic corrugations that ran far past the belt and moiréd from orbit. Gating by a slow noise and warping harder made them finite, irregular segments; the isotropic peaks carry most of the relief.

---

## 2026-08-23 — Mountains as Thrust Blocks

### What was done
Replaced the ridge-band mountains with tilted Voronoi blocks at three scales plus ridged peaks.

### Decisions and reasoning

**Blocks, not bands**
The user's critique: the belts looked like ripples in textile. Both smooth ridged noise and cosine bands are continuous, draped surfaces; real orogens are stacks of near-rigid sheets shoved over one another, with scarps between. Voronoi cells give convex blocks; a random height and tilt per cell gives thrust sheets; the discontinuity at cell edges is the scarp. This is the first deliberately discontinuous term in the height function — the hydrology grid handles it because it only samples heights, and the relief shading handles it because it is finite-difference. Finer block scales switch on with the octave count so orbit views stay cheap.

**From tiles to heaps**
The first block version (one tilted plane per Voronoi cell) read as stained glass: straight polygon edges and a uniform bevel band at every edge. Replaced by heaps — a per-cell pyramid under a random faceted norm, terrain = max over heaps. A Euclidean falloff was tried in between and gave cobblestones; angular norms were the difference. The heap stack is clamped non-negative so belts only add height; without that, heaps dipping below sea level shattered subduction coastlines into shard islands.

---

## 2026-08-23 — Mountain Look, Staircase Artifact, Frame Rate

### What was done
Iterated the rock-heap mountains to a stable form, fixed a staircase artifact from the plate texture, and roughly doubled frame rate in mountain belts.

### Decisions and reasoning

**Relief from screen-space derivatives**
The height function was evaluated three times per pixel (centre plus two offsets) for the shading normal. `dFdx`/`dFdy` of the surface point give the same normal from one evaluation. Largest single speed win; the only cost is 2×2-pixel quantisation of the normal, invisible in practice.

**Bicubic plate sampling**
Bilinear interpolation of the 40 km plate texture is continuous but has a slope kink at every texel edge; relief shading turned those kinks into a visible staircase across flat desert, and colour ramps traced texel-aligned contours at the shelf edge. Smoothstep-weighted bilinear removed the shading kinks but not the contours; bicubic B-spline (16 fetches, C², no overshoot) removed both.

**Heaps: 8 cells, three scales, gullies on top**
Feature points confined to the middle half of each cell let the heap search drop from 27 to 8 cells. Two finer heap scales (4 km, 1.3 km) were tried and removed — they produced curly slab edges that read as crumpled paper — in favour of an additive ridge-and-gully multifractal below ~10 km.

**Lake shoreline on the grid**
Two attempts to interpolate lake level across cells failed instructively. Carrying "height − 60 m" in non-lake cells flooded half the land, because the CPU's cell-centre height is nowhere near the GPU's within-cell terrain. Including the dilation ring in a weighted average made rounded-rectangle "pills". The working rule: any adjacent lake cell defines the level, the terrain decides the shore, lakes under three cells are dropped, and the drawn level sits 12 m above the spill. Flat-ground lakes still show the grid; recorded as a limitation.

**FPS in the title bar**
Added so performance claims are measured, not guessed: ~35 fps in belts before this session's changes, ~50 after, 60 (vsync) elsewhere.

---

## 2026-08-23 — Terrain Layers: Substrate and Vegetation

### What was done
Added substrate and potential-vegetation classification as functions of height, slope, climate, uplift and river proximity, rendered with smooth blending, mirrored on the CPU, with B/V debug overlays. Wrote [[Design/Terrain]].

### Decisions and reasoning

**Three layers, deviations only**
The user proposed a static ground layer with a vegetation layer on top. Adopted, with climate as a third derived layer between them and one rule added: nothing is stored except deviations from the functions. Cutting a forest stores a clearing with a start time; regrowth is a scheduled event. This is the design that lets terrain change without abandoning the function model, and it is the first mechanic that will need the event scheduler.

**Hard classes and soft rendering from the same variables**
The simulation needs an enum at a point; rendering needs no visible class edges. Both are cut from the same temperature/moisture/slope values, so a pixel's colour and its class agree even though the colour blends across thresholds.

**Climate in real units**
Temperature moved from a 0–1 "warmth" to °C with a lapse rate, so thresholds can be reasoned about (ice below −15 °C mean, treeline near −1 °C). The first threshold chosen (−10 °C) iced over a 3000 m mid-latitude plateau; lowered after checking the overlay.

**Coarse moisture noise**
Hard classification of a field with fine octaves speckles. Moisture now uses three octaves (features ≥ ~500 km), which keeps classes regional.
