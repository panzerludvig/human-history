# Globe Viewer

**Status:** Implemented — see [[Meta/Status Vocabulary]]

Version 1 of the game: a window showing a spherical, Earth-like globe that can be zoomed with the mouse wheel and panned with left-click drag. Code in `src/main.cpp`, `shaders/globe.vert`, `shaders/globe.frag`.

---

## Rendering

There is no mesh. The fragment shader draws a full-screen triangle, casts a ray per pixel from the camera, intersects it with a unit sphere, and shades the hit point. Everything about the surface — height, biome, relief — is a function of the unit surface normal, so detail is unlimited and there are no textures or tiles.

- **Terrain height:** domain-warped fBm for continents (biased to ~30% land), a finer fBm for detail, ridged noise for mountain ranges masked to continental interiors.
- **Noise:** 3D gradient noise with an integer hash (pcg3d). Integer hashing matters: a sin-based hash breaks at the large coordinates reached when zoomed to 10 km.
- **Level of detail:** the CPU computes km-per-pixel from the camera altitude and sets the octave count so the finest octave is about two pixels wide. 8 octaves at full-globe view, ~18 at max zoom.
- **Climate:** temperature from latitude and height; moisture from noise minus a subtropical dry band around ±25°. Drives sand/grass/forest/desert/tundra/snow, plus sea ice past ~73°.
- **Relief:** finite-difference height gradient at one-pixel spacing, with vertical exaggeration scaled to the zoom, perturbs the shading normal. Sun is fixed relative to the camera so the visible side is always lit.
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
- **Output per cell:** uplift (−1..1), crust (−1 oceanic .. 1 continental, blended over ~200 km), and distance in km to the nearest plate boundary. Uploaded as a bilinear RGBA32F texture on unit 1; the CPU mirror samples it with the same bilinear rule.
- **Into the function:** crust × 0.2 shifts the continent field, so coastlines tend to follow plate edges and the land % quantile includes it. Mountains are isotropic ridged noise plus ridge-and-valley bands that follow contours of the boundary distance (period ~45 km, strongly warped so spacing varies, gated by a slow noise so ridges are finite segments), scaled by uplift up to ~7000 m; negative uplift lowers the terrain for trenches and rifts. Ridges therefore run parallel to the belt without any directional frame — an earlier version stretched the noise along a per-cell belt direction, which smeared into concentric bands wherever the direction rotated. A small isotropic hills term remains.
- **Debug overlay:** press P in game (or pass `plates` as the 7th command-line argument) to tint the globe by crust (blue → tan) and uplift (red) / trench (cyan).

## Hydrology (lakes and rivers)

Lakes and rivers are non-local — a lake's level depends on its outlet, a river on everything upstream — so they cannot be read from the height function at a point. They are computed once per world (`src/hydrology.h`) and fed back into the function as a derived layer:

1. The height function is sampled on a 2048×1024 equirectangular grid (~20 km cells) using all CPU cores.
2. **Priority flood** from the ocean cells fills every depression; where the filled surface is above the ground, that cell is a lake at that level. The flood also records each cell's downstream neighbour, which routes flow correctly across flats and lake surfaces to the spill point.
3. **Basin breaching.** Full filling would make every broad basin an inland sea, but on Earth most basins have been breached by erosion. Each lake basin (connected component at one fill level) rolls a seeded number: 70% drain completely, 27% keep a lake capped at 60 m above the basin floor, 3% keep their full fill and become great lakes. This gives few very large lakes, more mid-sized ones.
4. **Flow accumulation** sums cell areas (km²) downstream in reverse flood order. Cells above a drainage threshold (12 000 km²) are river cells.
5. The table (lake level, drainage area, direction, height) is uploaded as one RGBA32F texel per cell.

In the shader, a pixel is water if its height is below sea level, below the lake level of its cell (lake levels spill one cell outward so the GPU's own height decides the shoreline, not the grid), or within a river. Rivers are drawn as lines between cell centres: each pixel checks the 5×5 cells around it for a river segment within half-width, with width from drainage area and a floor of ~1 pixel. Which rivers are drawn depends on zoom: from orbit only continental-scale rivers (≈10⁶ km² drainage) show, and the threshold falls as you zoom in until every river above 12 000 km² is visible.

**Ponds** — lakes far below the grid's 20 km resolution — come from the function itself: where a fine noise (~9 km wavelength) peaks on flat, low, moist land, the pixel is water a few metres above the ground. They are painted rather than routed; nothing flows into or out of them. Two noise displacements on the lookup point — a ~25 km bend and a ~7 km wiggle — hide the 8-direction grid.

Generation takes well under a second. Nothing is saved; the layer is rebuilt from the seed on load.

## Scale bar

Bottom-left, in game only. The distance is a 1/2/5 × 10ⁿ value chosen so the bar is close to 160 px at the current km-per-pixel (measured at the screen centre, so it is exact there and slightly off toward the limb at full-globe zoom). The bar is drawn by the fragment shader as a screen-space overlay; the label is a static control like the menu text.

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
- Lakes are numerous in mountainous noise terrain because nothing has eroded the basins away. A future erosion or basin-merging pass would thin them.
- Panning near the poles uses lat/lon deltas and gets distorted; acceptable for now.
