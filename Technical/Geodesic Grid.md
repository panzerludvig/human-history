# Geodesic Grid

**Status:** Implemented (the grid and its operators); the atmosphere on it is a reference port that no longer compiles — see [[Meta/Status Vocabulary]]

An icosahedron subdivided five times and projected onto the sphere, with the cells being the dual hexagons and twelve pentagons at the original corners. It exists because the climate model's latitude-longitude grid is singular at the poles: a cell's width goes as the cosine of latitude, so at 80 degrees a cell is 36 km wide against 208 km at the equator, and no single time step can be stable everywhere. Every polar failure the model has had traces to that one fact — cap rows acting as infinite reservoirs, a zonal filter that removed every mode except the jet, curvature terms, and (on 2026-09-07) a 70 m/s jet reaching the 80 degree wall of the quasi-geostrophic model and blowing up in four hours.

## Files

- `src/geodesic.h` — the grid (`geodesic::build(level)`), 3D tangent vectors, and the operators `grad`, `div`, `lap`, `advect`, each one loop over a cell's five or six neighbours with no special cases.
- `src/gridtest.cpp`, `build_gridtest.bat` — the self-test: mesh uniformity, operators against closed form, the stable step.
- `src/atmosphere_geo.h`, `src/geosweep.cpp`, `build_geosweep.bat` — the atmosphere of 2026-09-02 ported to the mesh, including its constants and helpers from `atmosphere.h`. It targets that day's physics (`H_UPPER`, `RHO_UPPER`) and does not compile against the two-layer, prescribed `atmosphere.h` of today. It is kept as the worked example of how the port is done, not as code to build.

## What the self-test measures (run of 2026-09-08)

| Quantity | Value |
|---|---|
| Cells at level 5 | 10242, uniform to within a few percent (about 250 km) |
| Hexagon area ratio, largest to smallest | 1.41 |
| Gradient of a linear field, worst error | 2 percent at one cell, 0.15 percent RMS |
| Laplacian, worst error | 0.016 percent |
| Stable step for gravity waves | 609 s everywhere |
| Same on the lat-lon grid at 87 degrees | 10 s |

The lat-lon grid spends 18432 cells and wastes its polar rows; the mesh has fewer cells and better effective resolution, and none of them pathological.

## What it gave on 2026-09-02

The atmosphere of that day, run on both grids against the same eight targets: error 73 on lat-lon against 35 on the mesh; the spurious 39 m/s polar easterly gone (0.6 m/s at 87 degrees); polar rain from 5.1 mm/day to 0.4. The heat diffusion constant that carried the whole eddy heat flux on the lat-lon grid turned out to be double-counting on the mesh, because the mesh resolves the flux. The ten-degree cold bias of that physics was unchanged by the port, as a global energy balance must be.

## What a port of today's model needs

The prescribed climate ([[Design/Weather]]) is a set of fields painted from latitude, distance to coast and terrain, none of which depends on the grid; the water transport and the rain rule are advection and local rules, which the mesh's `advect` covers. The quasi-geostrophic weather (`src/qg2.h`) inverts potential vorticity with a Fourier transform in longitude and a tridiagonal solve in latitude, which only exist on a lat-lon grid; on the mesh the Poisson and Helmholtz inversions need an iterative solver (multigrid or conjugate gradient on the mesh Laplacian). What disappears in return: the two channels and their walls, the free-slip mean-flow condition, the row taper, the polar filter. The equator remains: the QG approximation fails there and the tropics still need to be relaxed toward the painted belts. The globe shader samples the climate through a cell lookup and a centre function (see [[Dev Log/Log]], 2026-08-31), so the renderer needs a lookup from a unit vector to a mesh cell and nothing else.

## The port (2026-09-08)

- `src/geosolve.h`, `src/solvetest.cpp`, `build_solvetest.bat` — the elliptic solver: conjugate gradient on the mesh Laplacian with a Jacobi-plus-coarse-level preconditioner (162 aggregates, factored once). Round trip to one part in 10¹¹, discretisation 0.035 percent against spherical harmonics; 15 to 40 iterations per inversion in the model.
- `src/qg2geo.h`, `src/test_qg2geo.cpp`, `build_testqggeo.bat` — the two-layer quasi-geostrophic model on the mesh, one global domain. Flux-form advection with a corner-difference face flux that is nondivergent to rounding. Coupled to the Earth run by `QG2GEO` in `atmosphere.h`, `sweep.exe earth 0 geo 1`; a year is about seven minutes on top of the sweep's own.
- **Nothing of the lat-lon machinery survives:** no channels, no walls, no free-slip mean-flow condition, no row taper, no polar filter. A coupled Earth year ran stable at the first attempt.

What the target had to become, each step a full Earth year: a latitude factor to zero the thermal wind at the equator had a gradient of its own that confined the storms poleward of 58 degrees; referencing the column temperature to the equator's removed it; and replacing the single f0 at 45 degrees with the local f in the thermal wind, integrated from the equator, put the surface westerlies at 43 north and 47 south, the eddy energy peak at 43 to 58 north and 54 to 62 south, and the subtropical high at 43 north. The lat-lon channel model never got closer than 51 degrees for the westerlies.

Open against Earth: the northern surface westerlies are 3 to 5 degrees poleward of Earth's and weak at 43 to 51 (0 to 2 m/s against 4 to 6); Western Europe's rain is 4.5 mm/day in winter and 0.7 in summer where Earth's is even; the rain shadows of the Andes and Cascades leak under the gustier wind (Patagonia 8.6 mm/day, Great Basin 2.4), which is the rain rule and the same on both grids. The `TARGET_GAIN` of 1.5 carried over from the lat-lon calibration and has not been re-examined.

## Where it was, and why it was lost

Built on branch `climate-wind` on 2026-09-02 and never merged. The working line (`climate-rebuild`, then `prescribed-climate`) had forked the day before, and nothing in the vault recorded the mesh, so five days of atmosphere work — two air layers, the prescribed climate, the Earth template, a sigma-coordinate core and the quasi-geostrophic model — were built on the lat-lon grid. Brought onto `prescribed-climate` on 2026-09-08. The rule that follows is in [[Meta/Git]].
