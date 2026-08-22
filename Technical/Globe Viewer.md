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

## Known limitations

- Terrain at 10 km is low-contrast: the noise amplitude at fine octaves is small, so close-up land is mostly flat colour with faint relief. Tuning item, not a structural problem.
- Float precision on the GPU is adequate at max zoom but with little margin; zooming further would need camera-relative coordinates or double-precision uniforms.
- No terrain exists on the CPU — see the open question in [[Technical/Architecture]].
- Panning near the poles uses lat/lon deltas and gets distorted; acceptable for now.
