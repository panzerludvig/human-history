#version 330 core
// Raycasts a unit sphere and shades it with procedural, Earth-like terrain.
// Everything is a function of the surface point, so detail is unlimited; the
// number of noise octaves is chosen per frame from the zoom level.

in vec2 vNdc;
out vec4 fragColor;

uniform vec3 uCamPos;
uniform vec3 uForward;
uniform vec3 uRight;
uniform vec3 uUp;
uniform float uTanHalf;
uniform float uAspect;
uniform int uOctaves;
uniform float uKmPerPixel;
uniform mat3 uWorldRot;  // per-world rotation of the noise field
uniform vec3 uWorldOff;  // per-world offset (kept small for float precision)
uniform float uDim;      // 1 = normal, lower when a menu is over the globe
uniform float uFreq;     // continent field frequency
uniform float uWarp;     // domain-warp strength
uniform float uWebness;  // 0 = blobs, 1 = ridge web
uniform float uSeaLevel; // field value at the coastline, chosen on the CPU
uniform sampler2D uHydro; // per-cell lake level, drainage area, flow direction, height
uniform int uHasHydro;
uniform sampler2D uPlates; // uplift, crust, km to nearest plate boundary
uniform int uDebugPlates;  // 1 = tint the globe by plate crust and uplift
const float CRUST_WEIGHT = 0.2;
uniform vec4 uScaleBar;   // x0, y0, x1, y1 in pixels (bottom-left origin); x0 < 0 hides it

const float HEIGHT_SCALE_M = 8000.0;
const int HW = 2048, HH = 1024;     // hydrology grid size, must match hydrology.h
const float NO_LAKE = -1.0e6;

const float PI = 3.14159265;

// ------------------------------------------------------------ noise

// Integer hash (pcg3d) — stable at the large coordinates that deep zoom needs.
uvec3 pcg3d(uvec3 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return v;
}

vec3 gradient(ivec3 p) {
    uvec3 h = pcg3d(uvec3(p));
    return normalize(vec3(h) / 4294967295.0 * 2.0 - 1.0);
}

// 3D gradient noise, roughly in [-1, 1].
float noise(vec3 p) {
    ivec3 i = ivec3(floor(p));
    vec3 f = p - vec3(i);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float n000 = dot(gradient(i + ivec3(0, 0, 0)), f - vec3(0, 0, 0));
    float n100 = dot(gradient(i + ivec3(1, 0, 0)), f - vec3(1, 0, 0));
    float n010 = dot(gradient(i + ivec3(0, 1, 0)), f - vec3(0, 1, 0));
    float n110 = dot(gradient(i + ivec3(1, 1, 0)), f - vec3(1, 1, 0));
    float n001 = dot(gradient(i + ivec3(0, 0, 1)), f - vec3(0, 0, 1));
    float n101 = dot(gradient(i + ivec3(1, 0, 1)), f - vec3(1, 0, 1));
    float n011 = dot(gradient(i + ivec3(0, 1, 1)), f - vec3(0, 1, 1));
    float n111 = dot(gradient(i + ivec3(1, 1, 1)), f - vec3(1, 1, 1));
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z) * 1.6;
}

float fbm(vec3 p, int octaves, float gain) {
    float sum = 0.0, amp = 1.0, norm = 0.0;
    for (int i = 0; i < octaves; i++) {
        sum += amp * noise(p);
        norm += amp;
        amp *= gain;
        p = p * 2.03 + vec3(17.1, 31.7, 5.3);
    }
    return sum / norm;
}

// Ridged multifractal for mountain ranges.
float ridged(vec3 p, int octaves) {
    float sum = 0.0, amp = 0.5, norm = 0.0;
    for (int i = 0; i < octaves; i++) {
        float n = 1.0 - abs(noise(p));
        sum += amp * n * n;
        norm += amp;
        amp *= 0.55;
        p = p * 2.07 + vec3(3.3, 9.1, 21.7);
    }
    return sum / norm;
}

// ------------------------------------------------------------ terrain

// The raw continent field. Mirrored exactly in src/terrain.h — keep in sync.
float continentField(vec3 p) {
    vec3 warp = vec3(fbm(p * 1.3 + 11.0, 3, 0.5),
                     fbm(p * 1.3 + 23.0, 3, 0.5),
                     fbm(p * 1.3 + 37.0, 3, 0.5));
    vec3 q = p * uFreq + warp * uWarp;
    float base = fbm(q, 6, 0.5);
    // Ridges along the zero set of a second field: thin strips and chains.
    float web = 0.35 - abs(fbm(q + 53.0, 6, 0.5)) * 2.0;
    return mix(base, web, uWebness);
}

vec4 plateAt(vec3 n) {
    float lat = asin(clamp(n.z, -1.0, 1.0));
    float lon = atan(n.y, n.x);
    return texture(uPlates, vec2((lon + PI) / (2.0 * PI), (lat + PI / 2.0) / PI));
}

// Height in metres above sea level. `p` is the point in noise space, `n` the
// unit surface normal in world space. Mirrored in src/terrain.h — keep in sync.
float terrainHeight(vec3 p, vec3 n, int octaves) {
    vec4 pl = plateAt(n);
    float continent = continentField(p) + pl.g * CRUST_WEIGHT - uSeaLevel;
    float detail = fbm(p * 9.0 + 5.0, max(octaves - 3, 1), 0.5);

    // Mountain ranges: isotropic ridged noise plus parallel ridge-and-valley
    // bands that follow the contours of distance to the plate boundary —
    // a smooth scalar, so no frame can rotate and smear the noise.
    vec3 q = p * 7.0 + 2.0;
    float peaks = ridged(q, max(octaves - 2, 1));
    // Bands are gated by a slow noise so ridges are finite segments, and
    // warped so their spacing varies and neighbours merge.
    float band = pl.b / 45.0 + fbm(p * 6.0 + 61.0, 3, 0.5) * 2.5;
    float bands = 0.5 + 0.5 * cos(band * 6.2831853);
    float gate = smoothstep(0.0, 0.35, fbm(p * 5.0 + 83.0, 2, 0.5));
    float ranges = peaks * 0.8 + bands * bands * gate * peaks * 0.5;
    float hills = ridged(p * 4.0 + 2.0, max(octaves - 2, 1)) *
                  smoothstep(0.02, 0.25, continent) *
                  smoothstep(0.3, 0.7, fbm(p * 2.2 + 41.0, 3, 0.5) * 0.5 + 0.5);
    float uplift = pl.r;
    float h = continent + detail * 0.06 + ranges * max(uplift, 0.0) * 0.9 +
              min(uplift, 0.0) * 0.12 + hills * 0.12;
    return h * HEIGHT_SCALE_M;
}

// ------------------------------------------------------------ hydrology

ivec2 hydroCell(vec3 n) {
    float lat = asin(clamp(n.z, -1.0, 1.0));
    float lon = atan(n.y, n.x);
    int x = int(floor((lon + PI) / (2.0 * PI) * float(HW)));
    int y = int(floor((lat + PI / 2.0) / PI * float(HH)));
    return ivec2((x + HW) % HW, clamp(y, 0, HH - 1));
}

vec4 hydroFetch(ivec2 c) {
    c.x = (c.x + HW) % HW;
    if (c.y < 0 || c.y >= HH) return vec4(NO_LAKE, 0.0, -1.0, 0.0);
    return texelFetch(uHydro, c, 0);
}

vec3 cellCentre(ivec2 c) {
    float lat = (float(c.y) + 0.5) / float(HH) * PI - PI / 2.0;
    float lon = (float(c.x) + 0.5) / float(HW) * 2.0 * PI - PI;
    return vec3(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat));
}

const ivec2 DIRS[8] = ivec2[8](ivec2(1, 0), ivec2(1, 1), ivec2(0, 1), ivec2(-1, 1),
                               ivec2(-1, 0), ivec2(-1, -1), ivec2(0, -1), ivec2(1, -1));

// Distance (in units of the sphere radius) from n to the segment a-b.
float segmentDistance(vec3 n, vec3 a, vec3 b) {
    vec3 ab = b - a;
    float t = clamp(dot(n - a, ab) / max(dot(ab, ab), 1e-12), 0.0, 1.0);
    return length(n - (a + ab * t));
}

// River half-width in km for a drainage area in km^2, with a floor so rivers
// stay visible as lines when zoomed out.
float riverHalfWidthKm(float flowKm2) {
    float w = 0.0009 * sqrt(flowKm2);
    return max(w * 0.5, uKmPerPixel * 0.6);
}

// Returns the drainage area of the river under n (0 if none).
float riverAt(vec3 n, vec3 w) {
    // Wiggle the lookup point so rivers do not follow the grid exactly.
    // Two scales: a ~25 km bend that breaks up straight grid runs, and a
    // ~7 km wiggle for local meander.
    vec3 bend = vec3(noise(w * 140.0 + 3.0), noise(w * 140.0 + 17.0), noise(w * 140.0 + 29.0));
    vec3 wiggle = vec3(noise(w * 900.0 + 3.0), noise(w * 900.0 + 17.0), noise(w * 900.0 + 29.0));
    vec3 nj = normalize(n + bend * 0.0035 + wiggle * 0.0006);
    ivec2 c0 = hydroCell(nj);
    // Zoomed out, only continental rivers are drawn; more appear as you zoom in.
    float minFlow = 12000.0 * pow(max(uKmPerPixel / 0.5, 1.0), 1.5);
    float best = 0.0;
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            ivec2 c = c0 + ivec2(dx, dy);
            vec4 t = hydroFetch(c);
            if (t.g < minFlow || t.b < 0.0) continue;
            ivec2 d = c + DIRS[int(t.b + 0.5)];
            float dist = segmentDistance(nj, cellCentre(c), cellCentre(d)) * 6371.0;
            if (dist < riverHalfWidthKm(t.g)) best = max(best, t.g);
        }
    return best;
}

// ------------------------------------------------------------ shading

vec3 terrainColor(vec3 n, float h, float slope, float lat) {
    float absLat = abs(lat);
    // Climate: temperature falls with latitude and altitude; moisture from noise.
    float temp = 1.0 - pow(absLat / (PI / 2.0), 1.3) - max(h, 0.0) / HEIGHT_SCALE_M * 0.9;
    float moisture = fbm(n * 3.0 + 77.0, 4, 0.5) * 0.5 + 0.5;
    // Subtropical dry bands around +-25 degrees.
    float dryBand = exp(-pow((absLat - 0.42) / 0.16, 2.0));
    moisture = moisture - dryBand * 0.45;

    vec3 sand = vec3(0.80, 0.72, 0.50);
    vec3 grass = vec3(0.30, 0.48, 0.20);
    vec3 forest = vec3(0.12, 0.30, 0.12);
    vec3 desert = vec3(0.76, 0.62, 0.40);
    vec3 tundra = vec3(0.55, 0.52, 0.42);
    vec3 rock = vec3(0.45, 0.42, 0.38);
    vec3 snow = vec3(0.94, 0.95, 0.97);

    vec3 veg = mix(grass, forest, smoothstep(0.45, 0.75, moisture));
    vec3 c = mix(desert, veg, smoothstep(0.25, 0.45, moisture));
    c = mix(c, tundra, smoothstep(0.30, 0.12, temp));
    c = mix(sand, c, smoothstep(0.0, 100.0, h));
    c = mix(c, rock, smoothstep(0.25, 0.6, slope));
    // High ground reads as rock from any distance.
    c = mix(c, rock, smoothstep(1800.0, 3200.0, h) * 0.8);
    // Snow where it is cold: polar lowlands and high peaks everywhere.
    c = mix(c, snow, smoothstep(0.16, 0.06, temp + slope * 0.1));
    return c;
}

vec3 waterColor(float depthM, float lat, vec3 n) {
    float depth = clamp(depthM / 1300.0, 0.0, 1.0);
    vec3 shallow = vec3(0.18, 0.52, 0.68);
    vec3 deep = vec3(0.02, 0.10, 0.30);
    vec3 c = mix(shallow, deep, sqrt(depth));
    if (abs(lat) > 1.28 + 0.06 * noise(n * 6.0)) c = vec3(0.85, 0.9, 0.95); // sea ice
    return c;
}

// Scale bar overlay: a white line with end ticks and a dark outline.
vec3 scaleBarOverlay(vec3 col) {
    if (uScaleBar.x < 0.0) return col;
    vec2 f = gl_FragCoord.xy;
    float onLine = (f.x >= uScaleBar.x && f.x <= uScaleBar.z && abs(f.y - uScaleBar.y) <= 1.5) ? 1.0 : 0.0;
    float tick = ((abs(f.x - uScaleBar.x) <= 1.5 || abs(f.x - uScaleBar.z) <= 1.5) && abs(f.y - uScaleBar.y) <= 6.0) ? 1.0 : 0.0;
    float outline = (f.x >= uScaleBar.x - 1.5 && f.x <= uScaleBar.z + 1.5 && abs(f.y - uScaleBar.y) <= 3.0) ||
                    ((abs(f.x - uScaleBar.x) <= 3.0 || abs(f.x - uScaleBar.z) <= 3.0) && abs(f.y - uScaleBar.y) <= 7.5) ? 1.0 : 0.0;
    col = mix(col, vec3(0.05), outline * 0.85);
    return mix(col, vec3(0.95), max(onLine, tick));
}

void main() {
    vec3 dir = normalize(uForward + uRight * vNdc.x * uTanHalf * uAspect + uUp * vNdc.y * uTanHalf);
    float b = dot(uCamPos, dir);
    float c = dot(uCamPos, uCamPos) - 1.0;
    float disc = b * b - c;

    vec3 bg = vec3(0.01, 0.01, 0.03);
    if (disc < 0.0) {
        // Thin atmospheric halo just outside the limb.
        float miss = sqrt(-disc);
        float glow = exp(-miss * 60.0 / max(length(uCamPos) - 1.0, 0.02));
        fragColor = vec4(scaleBarOverlay((bg + vec3(0.25, 0.45, 0.8) * glow * 0.6) * uDim), 1.0);
        return;
    }
    float t = -b - sqrt(disc);
    vec3 p = uCamPos + dir * t;
    vec3 n = normalize(p);
    float lat = asin(clamp(n.z, -1.0, 1.0));

    // Terrain is sampled in a per-world noise space.
    vec3 w = uWorldRot * n + uWorldOff;
    float h = terrainHeight(w, n, uOctaves);

    // Water: the sea, a lake surface from the hydrology grid, or a river.
    float waterLevel = 0.0;
    bool isWater = h < 0.0;
    bool isRiver = false;
    if (uHasHydro == 1) {
        vec4 cell = hydroFetch(hydroCell(n));
        if (cell.r > NO_LAKE + 1.0 && h < cell.r) { isWater = true; waterLevel = cell.r; }
        if (!isWater && h > 0.0 && riverAt(n, w) > 0.0) { isWater = true; isRiver = true; waterLevel = h; }
    }
    // Ponds: tiny lakes below the grid's resolution, where a fine noise
    // peaks on flat, moist, low ground. Painted by the function, not routed.
    bool isPond = false;
    if (!isWater && h > 0.0 && h < 1500.0) {
        float pondField = fbm(w * 700.0 + 91.0, 3, 0.5);
        float flatness = 1.0 - smoothstep(0.0, 0.015, abs(terrainHeight(uWorldRot * normalize(n + vec3(0.0005)) + uWorldOff, normalize(n + vec3(0.0005)), max(uOctaves - 4, 4)) - h) / 80.0);
        float moist = fbm(w * 3.0 + 77.0, 4, 0.5) * 0.5 + 0.5;
        if (pondField > 0.26 + 0.22 * (1.0 - moist) && flatness > 0.3) { isWater = true; isPond = true; waterLevel = h + 3.0; }
    }

    // Relief: finite-difference gradient in the tangent plane, step ~ one pixel.
    float eps = max(uKmPerPixel / 6371.0, 1e-6);
    vec3 tx = normalize(cross(n, abs(n.z) < 0.99 ? vec3(0, 0, 1) : vec3(1, 0, 0)));
    vec3 ty = cross(n, tx);
    vec3 nx = normalize(n + tx * eps), ny = normalize(n + ty * eps);
    float hx = terrainHeight(uWorldRot * nx + uWorldOff, nx, uOctaves);
    float hy = terrainHeight(uWorldRot * ny + uWorldOff, ny, uOctaves);
    // Vertical exaggeration keeps relief visible at every zoom.
    float reliefScale = 0.004 / eps / HEIGHT_SCALE_M;
    vec3 gradT = (tx * (hx - h) + ty * (hy - h)) * reliefScale;
    vec3 shadeN = normalize(n - gradT);
    // Physical slope (rise over run) decides rock; the exaggerated normal only shades.
    float slope = clamp(length(vec2(hx - h, hy - h)) / (eps * 6371000.0) * 2.0, 0.0, 1.0);
    if (isWater) { shadeN = n; slope = 0.0; }

    vec3 albedo;
    if (uDebugPlates == 1) {
        // Crust: oceanic blue to continental tan; uplift: red, trench/rift: cyan.
        vec4 pl = plateAt(n);
        vec3 base = mix(vec3(0.15, 0.25, 0.55), vec3(0.65, 0.55, 0.35), pl.g * 0.5 + 0.5);
        if (isWater && !isRiver) base *= 0.6;
        base = mix(base, vec3(0.9, 0.15, 0.1), clamp(pl.r, 0.0, 1.0));
        base = mix(base, vec3(0.1, 0.9, 0.9), clamp(-pl.r, 0.0, 1.0));
        fragColor = vec4(scaleBarOverlay(base * uDim), 1.0);
        return;
    }
    if (isRiver) albedo = vec3(0.10, 0.30, 0.48);
    else if (isPond) albedo = vec3(0.14, 0.36, 0.50);
    else if (isWater) albedo = waterColor(waterLevel - h, lat, w);
    else albedo = terrainColor(w, h, slope, lat);

    // Sun fixed relative to the camera so the visible side is always lit.
    vec3 sun = normalize(-uForward * 0.45 + uRight * -0.6 + uUp * 0.65);
    float diff = max(dot(shadeN, sun), 0.0);
    float limb = pow(1.0 - max(dot(n, -dir), 0.0), 3.0);
    vec3 col = albedo * (0.25 + 0.8 * diff);
    col += vec3(0.3, 0.5, 0.9) * limb * 0.35 * smoothstep(0.0, 0.6, length(uCamPos) - 1.0);

    col = pow(col, vec3(1.0 / 1.6)) * uDim;
    fragColor = vec4(scaleBarOverlay(col), 1.0);
}
