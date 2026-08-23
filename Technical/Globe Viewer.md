# Globe Viewer

**Status:** Implemented — see [[Meta/Status Vocabulary]]

Version 1 of the game: a window showing a spherical, Earth-like globe that can be zoomed with the mouse wheel and panned with left-click drag. Code in `src/main.cpp`, `shaders/globe.vert`, `shaders/globe.frag`.

---

## Rendering

There is no mesh. The fragment shader draws a full-screen triangle, casts a ray per pixel from the camera, intersects it with a unit sphere, and shades the hit point. Everything about the surface — height, biome, relief — is a function of the unit surface normal, so detail is unlimited and there are no textures or tiles.

- **Terrain height:** domain-warped fBm for continents (biased to ~30% land), a finer fBm for detail, ridged noise for mountain ranges masked to continental interiors.
- **Noise:** 3D gradient noise with an integer hash (pcg3d). Integer hashing matters: a sin-based hash breaks at the large coordinates reached when zoomed to 10 km.
- **Level of detail:** the CPU computes km-per-pixel from the camera altitude and sets the octave count so the finest octave is about two pixels wide. 8 octaves at full-globe view, ~18 at max zoom.
- **Climate:** mean temperature in °C from latitude (28 °C at the equator, −17 °C at the poles) with a 6.5 °C/km lapse rate; moisture 0–1 from a coarse noise minus a subtropical dry band around ±25°. Sea ice past ~73°.
- **Substrate and vegetation** ([[Design/Terrain]]): hard classes for the simulation — substrate ∈ {soil, sand, rock, scree, silt, mud, ice} from height, slope, temperature, moisture, uplift and river proximity; vegetation ∈ {none, tundra, taiga, forest, rainforest, grassland, steppe, savanna, shrub, marsh, desert} from substrate and climate. Both are mirrored in `src/terrain.h`. Rendering blends the same variables smoothly: a substrate colour underneath, a vegetation colour on top weighted by cover (moisture × warmth × not-rock). **B** and **V** in game show the hard classes as flat colours.
- **Relief:** the shading normal comes from screen-space derivatives (`dFdx`/`dFdy`) of the surface point with height exaggerated 3×, so the height function is evaluated once per pixel. Rock colour uses the physical slope from the same derivatives plus an altitude tint. Sun is fixed relative to the camera so the visible side is always lit.
- **Atmosphere:** limb brightening on the sphere and a thin glow just outside it.

## Tectonic plates

The first generation step (`src/plates.h`), before sea level and hydrology. Global information — which plate a point is on and what its neighbour is doing — so it is a layer, not part of the function; the function samples it.

- **Plates:** 10–17 Voronoi cells on the sphere, edges warped by a value noise so they are not straight. Each has an Euler pole (axis × rate, giving a velocity field `ω × p`) and is continental (45 %) or oceanic.
- **Boundaries:** for each cell of a 1024×512 grid, the two nearest plates A and B; distance to the boundary; the relative velocity of A against B projected on the boundary normal gives convergence. Profiles are functions of signed distance so they are continuous across the boundary:
  - continental–continental convergence: wide symmetric uplift (380 km);
  - oceanic–continental: uplift peaking 160 km inland on the continental side, a trench 90 km out to sea (Andes);
  - oceanic–oceanic: arc on the overriding plate, trench on the other;
  - divergent: mid-ocean ridge rise at sea, rift dip on land.
  Belt strength varies along the boundary so ranges have ends.
- **Output per cell:** uplift (−1..1), crust (−1 oceanic .. 1 continental, blended over ~200 km), and distance in km to the nearest plate boundary. Uploaded as an RGBA32F texture on unit 1 and sampled with a bicubic B-spline (C², no overshoot) on both CPU and GPU — plain bilinear has a slope kink at every texel edge that relief shading draws as a staircase.
- **Into the function:** crust × 0.2 shifts the continent field, so coastlines tend to follow plate edges and the land % quantile includes it. Mountains are *rock heaps*: every lattice cell at three scales (~100 km, ~35 km, ~12 km; finer scales switch on with zoom) owns a lopsided pyramid — height falling off from a random centre under a random faceted norm (three random axes), tilted so one side is steep — and the terrain takes the highest heap at each point, so creases between heaps are sharp and irregular. Feature points stay in the middle half of their cell so only the 2×2×2 surrounding cells need checking. Jagged ridged peaks modulate the heaps, and below ~10 km a ridge-and-gully multifractal is added on the faces. Heaps appear only where uplift is substantial, never cut below the belt baseline, and the stack is scaled by uplift up to ~7000 m; negative uplift lowers the terrain for trenches and rifts. No directional frame is used — an earlier version stretched the noise along a per-cell belt direction, which smeared into concentric bands wherever the direction rotated; a cosine-band version after that looked like folded cloth. A small isotropic hills term remains.
- **Debug overlays:** P = plates (crust blue → tan, uplift red, trench cyan), B = substrate classes, V = vegetation classes; or pass `plates` / `substrate` / `vegetation` as the 7th command-line argument.

## Hydrology (lakes and rivers)

Lakes and rivers are non-local — a lake's level depends on its outlet, a river on everything upstream — so they cannot be read from the height function at a point. They are computed once per world (`src/hydrology.h`) and fed back into the function as a derived layer:

1. The height function is sampled on a 2048×1024 equirectangular grid (~20 km cells) using all CPU cores.
2. **Priority flood** from the ocean cells fills every depression; where the filled surface is above the ground, that cell is a lake at that level. The flood also records each cell's downstream neighbour, which routes flow correctly across flats and lake surfaces to the spill point.
3. **Basin breaching.** Full filling would make every broad basin an inland sea, but on Earth most basins have been breached by erosion. Each lake basin (connected component at one fill level) rolls a seeded number: 70% drain completely, 27% keep a lake capped at 60 m above the basin floor, 3% keep their full fill and become great lakes. This gives few very large lakes, more mid-sized ones.
4. **Flow accumulation** sums cell areas (km²) downstream in reverse flood order. Cells above a drainage threshold (12 000 km²) are river cells.
5. The table (lake level, drainage area, direction, height) is uploaded as one RGBA32F texel per cell.

In the shader, a pixel is water if its height is below sea level, below the lake level around it (a smooth-weighted average over the adjacent lake cells, drawn 12 m above the spill so flat shores flood irregularly; lakes under three cells are dropped), or within a river (only searched where a per-cell flag says a river is within two cells). Rivers are drawn as lines between cell centres: each pixel checks the 5×5 cells around it for a river segment within half-width, with width from drainage area and a floor of ~1 pixel. Which rivers are drawn depends on zoom: from orbit only continental-scale rivers (≈10⁶ km² drainage) show, and the threshold falls as you zoom in until every river above 12 000 km² is visible.

**Ponds** — lakes far below the grid's 20 km resolution — come from the function itself: where a fine noise (~9 km wavelength) peaks on flat, low, moist land, the pixel is water a few metres above the ground. They are painted rather than routed; nothing flows into or out of them. Two noise displacements on the lookup point — a ~25 km bend and a ~7 km wiggle — hide the 8-direction grid.

Generation takes well under a second. Nothing is saved; the layer is rebuilt from the seed on load.

## Scale bar

Bottom-left, in game only. The distance is a 1/2/5 × 10ⁿ value chosen so the bar is close to 160 px at the current km-per-pixel (measured at the screen centre, so it is exact there and slightly off toward the limb at full-globe zoom). The bar is drawn by the fragment shader as a screen-space overlay; the label is a static control like the menu text.

## Tooltip

In game, a label follows the cursor with what is under it: elevation, mean temperature, and terrain ("forest on soil", "rock", "Sea, 354 m deep", "Lake, 20 m deep"). It is computed on the CPU from the terrain mirror (`terrain.h`) at the current level of detail, using the same lake rule as the shader, so it doubles as a live check that the CPU and GPU terrain agree.

## Camera

Sits at (lat, lon, altitude) above the surface and always looks at the globe's centre, so at deep zoom it is looking straight down. Vertical field of view 45°.

- **Max zoom in:** altitude chosen so the screen width covers 10 km (depends on aspect ratio).
- **Max zoom out:** altitude chosen so the full disc fits with a small margin — one side of the globe, not much more.
- **Pan:** on mouse-down, the surface point under the cursor is recorded; on drag, the camera rotates so that point stays under the cursor. Off-globe drags fall back to a pixel-proportional rotation. Latitude is clamped to ±89° so the poles can't flip the camera.

## Menus and worlds

Menus are native Win32 controls (buttons, a list box, static text) laid over the GL window and shown/hidden per screen. Zero dependencies, text rendering for free; they look like Windows, which is fine until the game has a visual identity.

- **Main menu:** New World (opens the generation screen: seed with Random button, land %, concentration %; Generate or Enter), Load World (list of saves: Load or double-click, Delete with confirmation, Back), Quit.
- **In game:** Esc opens the pause menu over the dimmed globe: a name box (pre-filled, Enter saves), Save World, Main Menu, Quit Game. Esc again returns to the game.
- **A world** is a 32-bit seed plus the camera position. The seed derives a rotation matrix and a small offset applied to the surface normal before noise lookup, so each seed is a different globe from the same terrain code. The offset is kept within ±2 because large offsets cost float precision at deep zoom.
- **Save files** are plain text in `build\worlds\<name>.ibw` (`version`, `seed`, `land`, `concentration`, `lat`, `lon`, `altitude`; missing keys take defaults). A new world starts named `world-<seed>`; the name box lets you change it before saving, and the name becomes the file name (characters illegal in file names are replaced with `_`). Saving under a new name writes a new file and leaves the old one in place.

## Known limitations

- Terrain at 10 km is low-contrast: the noise amplitude at fine octaves is small, so close-up land is mostly flat colour with faint relief. Tuning item, not a structural problem.
- Float precision on the GPU is adequate at max zoom but with little margin; zooming further would need camera-relative coordinates or double-precision uniforms.
- The full height function exists on the CPU (`terrain.h`) but there is no mechanism to enforce that it matches the shader; divergence would show as rivers running slightly off their valleys.
- Rivers are not carved into the terrain; they are painted on it. Banks and valleys will need the overlay mechanism in [[Meta/Open Threads]].
- Lakes on flat ground still show the 20 km grid: a lake whose basin is a few flat cells is drawn as those cells. A finer hydrology grid or a sub-cell basin shape would fix it.
- Frame rate in mountain belts is ~50 fps at 1280×720 against 60 elsewhere (title bar shows fps). Resolution scaling or a cheaper heap function are the next levers.
- Panning near the poles uses lat/lon deltas and gets distorted; acceptable for now.
