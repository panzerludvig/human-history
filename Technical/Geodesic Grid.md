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

## Where it was, and why it was lost

Built on branch `climate-wind` on 2026-09-02 and never merged. The working line (`climate-rebuild`, then `prescribed-climate`) had forked the day before, and nothing in the vault recorded the mesh, so five days of atmosphere work — two air layers, the prescribed climate, the Earth template, a sigma-coordinate core and the quasi-geostrophic model — were built on the lat-lon grid. Brought onto `prescribed-climate` on 2026-09-08. The rule that follows is in [[Meta/Git]].
