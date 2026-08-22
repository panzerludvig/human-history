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

## Camera

Sits at (lat, lon, altitude) above the surface and always looks at the globe's centre, so at deep zoom it is looking straight down. Vertical field of view 45°.

- **Max zoom in:** altitude chosen so the screen width covers 10 km (depends on aspect ratio).
- **Max zoom out:** altitude chosen so the full disc fits with a small margin — one side of the globe, not much more.
- **Pan:** on mouse-down, the surface point under the cursor is recorded; on drag, the camera rotates so that point stays under the cursor. Off-globe drags fall back to a pixel-proportional rotation. Latitude is clamped to ±89° so the poles can't flip the camera.

## Menus and worlds

Menus are native Win32 controls (buttons, a list box, static text) laid over the GL window and shown/hidden per screen. Zero dependencies, text rendering for free; they look like Windows, which is fine until the game has a visual identity.

- **Main menu:** New World, Load World (list of saves, double-click or Load), Quit.
- **In game:** Esc opens the pause menu over the dimmed globe: Save World, Main Menu, Quit Game. Esc again returns to the game.
- **A world** is a 32-bit seed plus the camera position. The seed derives a rotation matrix and a small offset applied to the surface normal before noise lookup, so each seed is a different globe from the same terrain code. The offset is kept within ±2 because large offsets cost float precision at deep zoom.
- **Save files** are plain text in `build\worlds\<name>.ibw` (`seed`, `lat`, `lon`, `altitude`). A new world is named `world-<seed>`; saving overwrites that file. There is no rename or save-as yet.

## Known limitations

- Terrain at 10 km is low-contrast: the noise amplitude at fine octaves is small, so close-up land is mostly flat colour with faint relief. Tuning item, not a structural problem.
- Float precision on the GPU is adequate at max zoom but with little margin; zooming further would need camera-relative coordinates or double-precision uniforms.
- No terrain exists on the CPU — see the open question in [[Technical/Architecture]].
- Panning near the poles uses lat/lon deltas and gets distorted; acceptable for now.
- Saves have no name entry and no delete; one save per generated world.
