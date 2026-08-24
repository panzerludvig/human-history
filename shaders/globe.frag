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
uniform sampler2D uPop;    // carrying capacity K, settlement population, band population
uniform vec3 uSun;         // world-space sun direction (day-night and seasons)
uniform sampler2D uClim;   // climatology, 4 season bands: cloud, rain, wind u, wind v
uniform sampler2D uClim2;  // climatology 2: mean T, snowfall, coarse elevation
uniform float uDoy;        // day of year, 0..365
uniform float uClock;      // sim days (mod 4096) for cloud drift
uniform vec4 uAware[8];    // selected entities: xyz unit position, w = radius km
uniform int uAwareCount;
uniform int uDebugMode;    // 0 normal, 1 plates, 2 substrate, 3 vegetation
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

// Hash of a lattice cell to three values in [0, 1).
vec3 hash01(ivec3 c) {
    uvec3 h = pcg3d(uvec3(c));
    return vec3(h) / 4294967296.0;
}

// Thrust blocks: Voronoi cells, each a tilted slab at its own random height.
// Cell edges are discontinuities — scarps where one block rides over the next.
// Mirrored in src/terrain.h — keep in sync.
// Rock heaps: every lattice cell owns a lopsided heap — height falling off
// from a random centre, tilted so one side is steep — and the terrain is the
// highest heap at each point. Where heaps meet, the crease is sharp and
// irregular: rock shoved against rock, with no uniform edge band.
// Mirrored in src/terrain.h — keep in sync.
// Feature points sit in the middle half of their cell, so the 2x2x2 cells
// around the point (found by rounding) hold every heap that can reach it.
float blocks(vec3 p) {
    ivec3 c = ivec3(floor(p - 0.5));
    vec3 f = p - vec3(c);
    float best = -1e9;
    for (int dz = 0; dz <= 1; dz++)
        for (int dy = 0; dy <= 1; dy++)
            for (int dx = 0; dx <= 1; dx++) {
                ivec3 cell = c + ivec3(dx, dy, dz);
                vec3 d = f - (vec3(dx, dy, dz) + hash01(cell) * 0.5 + 0.25);
                vec3 r = hash01(cell * 3 + ivec3(1, 7, 13));
                vec3 t2 = hash01(cell + ivec3(5, -3, 9));
                vec3 tilt = vec3(r.y * 2.0 - 1.0, r.z * 2.0 - 1.0, t2.x * 2.0 - 1.0);
                // Faceted norm: three random axes make the heap a pyramid with
                // sharp edges rather than a round dome.
                vec3 a1 = vec3(1.0, t2.y * 2.0 - 1.0, t2.z * 2.0 - 1.0);
                vec3 a2 = vec3(t2.z * 2.0 - 1.0, 1.0, r.y * 2.0 - 1.0);
                vec3 a3 = vec3(r.z * 2.0 - 1.0, t2.x * 2.0 - 1.0, 1.0);
                float dist = max(max(abs(dot(d, a1)), abs(dot(d, a2))), abs(dot(d, a3)));
                float slope = 1.3 + t2.y * 0.9;
                float h = r.x * 0.7 + 0.3 - dist * slope + dot(d, tilt) * 0.5;
                best = max(best, h);
            }
    return best;
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

// Blocks at a given frequency with a domain warp so edges are crooked.
float warpedBlocks(vec3 p, float freq, float seedOff) {
    vec3 q = p * freq;
    vec3 w = vec3(fbm(q * 0.7 + seedOff, 2, 0.55), fbm(q * 0.7 + seedOff + 7.0, 2, 0.55),
                  fbm(q * 0.7 + seedOff + 19.0, 2, 0.55));
    return blocks(q + w * 0.4 + seedOff);
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

const int PW = 1024, PH = 512; // plate grid size, must match plates.h

// Bicubic B-spline: C2 and free of overshoot, so neither relief shading nor
// colour ramps show the texel grid. Mirrored in plates.h Field::sample.
vec4 bsplineWeights(float t) {
    float t2 = t * t, t3 = t2 * t;
    return vec4(1.0 - 3.0 * t + 3.0 * t2 - t3, 4.0 - 6.0 * t2 + 3.0 * t3,
                1.0 + 3.0 * t + 3.0 * t2 - 3.0 * t3, t3) / 6.0;
}

vec4 plateAt(vec3 n) {
    float lat = asin(clamp(n.z, -1.0, 1.0));
    float lon = atan(n.y, n.x);
    vec2 uv = vec2((lon + PI) / (2.0 * PI) * float(PW), (lat + PI / 2.0) / PI * float(PH)) - 0.5;
    ivec2 i = ivec2(floor(uv));
    vec2 f = uv - vec2(i);
    vec4 wx = bsplineWeights(f.x), wy = bsplineWeights(f.y);
    vec4 sum = vec4(0.0);
    for (int j = 0; j < 4; j++) {
        int y = clamp(i.y - 1 + j, 0, PH - 1);
        for (int k = 0; k < 4; k++) {
            int x = ((i.x - 1 + k) % PW + PW) % PW;
            sum += texelFetch(uPlates, ivec2(x, y), 0) * (wx[k] * wy[j]);
        }
    }
    return sum;
}

// Height in metres above sea level. `p` is the point in noise space, `n` the
// unit surface normal in world space. Mirrored in src/terrain.h — keep in sync.
float terrainHeight(vec3 p, vec3 n, int octaves) {
    vec4 pl = plateAt(n);
    float continent = continentField(p) + pl.g * CRUST_WEIGHT - uSeaLevel;
    float detail = fbm(p * 9.0 + 5.0, max(octaves - 3, 1), 0.5);

    // Mountain ranges: thrust blocks at three scales (sheets ~100 km,
    // blocks ~35 km, slabs ~12 km) with jagged ridged peaks on top. The
    // blocks are discontinuous at their edges by design: rock pushed over rock.
    vec3 q = p * 7.0 + 2.0;
    float peaks = ridged(q, max(octaves - 3, 1));
    float uplift = pl.r;
    // Blocks appear only where uplift is substantial; lowlands keep peaks/hills.
    float blockMask = smoothstep(0.35, 0.75, uplift);
    float stack = 0.5;
    if (blockMask > 0.0) {
        float sheets = warpedBlocks(p, 60.0, 3.0);
        float mid = octaves >= 7 ? warpedBlocks(p, 180.0, 11.0) : 0.5;
        float slabs = octaves >= 10 ? warpedBlocks(p, 500.0, 23.0) : 0.5;
        // Uplift only ever adds: heaps never cut below the belt's baseline.
        stack = max(sheets * 0.45 + mid * 0.3 + slabs * 0.25, 0.05);
    }
    // Below ~10 km the rock is ridges and gullies carved into the heap
    // faces: a ridged multifractal added on top, not more blocks.
    float gullies = octaves >= 11 ? ridged(p * 60.0 + 5.0, max(octaves - 8, 1)) - 0.45 : 0.0;
    float ranges = stack * blockMask * 0.7 * (0.55 + 0.45 * peaks) + peaks * 0.5 +
                   gullies * blockMask * 0.4;
    float hills = ridged(p * 4.0 + 2.0, clamp(octaves - 2, 1, 6)) *
                  smoothstep(0.02, 0.25, continent) *
                  smoothstep(0.3, 0.7, fbm(p * 2.2 + 41.0, 3, 0.5) * 0.5 + 0.5);
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

// Lake level around n: a smooth-weighted average over the 2x2 cells that
// are lakes. Where lake cells carry less than a third of the weight there is
// no lake, so the shoreline follows a rounded contour, not the cell grid.
// Returns NO_LAKE if none.
float lakeLevelAt(vec3 n) {
    float lat = asin(clamp(n.z, -1.0, 1.0));
    float lon = atan(n.y, n.x);
    vec2 uv = vec2((lon + PI) / (2.0 * PI) * float(HW), (lat + PI / 2.0) / PI * float(HH)) - 0.5;
    ivec2 i = ivec2(floor(uv));
    vec2 f = uv - vec2(i);
    f = f * f * (3.0 - 2.0 * f);
    float lv[4] = float[4](hydroFetch(i).r, hydroFetch(i + ivec2(1, 0)).r,
                           hydroFetch(i + ivec2(0, 1)).r, hydroFetch(i + ivec2(1, 1)).r);
    float wt[4] = float[4]((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
    float sum = 0.0, wsum = 0.0;
    for (int k = 0; k < 4; k++)
        if (lv[k] > NO_LAKE + 1.0) { sum += lv[k] * wt[k]; wsum += wt[k]; }
    // Any adjacent lake cell defines a level; the terrain decides the shore.
    return wsum > 0.001 ? sum / wsum : NO_LAKE;
}

vec3 cellCentre(ivec2 c) {
    float lat = (float(c.y) + 0.5) / float(HH) * PI - PI / 2.0;
    float lon = (float(c.x) + 0.5) / float(HW) * 2.0 * PI - PI;
    return vec3(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat));
}

const ivec2 DIRS[8] = ivec2[8](ivec2(1, 0), ivec2(1, 1), ivec2(0, 1), ivec2(-1, 1),
                               ivec2(-1, 0), ivec2(-1, -1), ivec2(0, -1), ivec2(1, -1));

// Population of a settlement whose marker covers n, else 0.
float settlementNear(vec3 n) {
    ivec2 c0 = hydroCell(n);
    float radiusKm = clamp(uKmPerPixel * 5.0, 4.0, 18.0);
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            ivec2 c = c0 + ivec2(dx, dy);
            ivec2 cw = ivec2((c.x + HW) % HW, clamp(c.y, 0, HH - 1));
            float p = texelFetch(uPop, cw, 0).g;
            if (p <= 0.0) continue;
            float dist = length(n - cellCentre(cw)) * 6371.0;
            if (dist < radiusKm) return p;
        }
    return 0.0;
}

// Population of a migrating band whose marker covers n, else 0.
float bandNear(vec3 n) {
    ivec2 c0 = hydroCell(n);
    float radiusKm = clamp(uKmPerPixel * 4.0, 3.0, 14.0);
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            ivec2 c = c0 + ivec2(dx, dy);
            ivec2 cw = ivec2((c.x + HW) % HW, clamp(c.y, 0, HH - 1));
            float p = texelFetch(uPop, cw, 0).b;
            if (p <= 0.0) continue;
            float dist = length(n - cellCentre(cw)) * 6371.0;
            if (dist < radiusKm) return p;
        }
    return 0.0;
}

// Climate lookups are warped by a small noise (~60 km) so the coarse grid's
// bilinear creases become organic wiggles. Mirrors atmosphere::climFuzz.
vec3 climFuzz(vec3 n) {
    vec3 o = vec3(fbm(n * 23.0 + 5.0, 2, 0.5), fbm(n * 23.0 + 11.0, 2, 0.5),
                  fbm(n * 23.0 + 17.0, 2, 0.5));
    return normalize(n + o * 0.010);
}

// Season-interpolated climatology sample at a surface point.
vec4 climSample(sampler2D tex, vec3 nRaw) {
    vec3 n = climFuzz(nRaw);
    float lat = asin(clamp(n.z, -1.0, 1.0));
    float lon = atan(n.y, n.x);
    float cx = (lon + 3.14159265) / 6.2831853;
    float cy = clamp((lat + 1.5707963) / 3.14159265, 0.02, 0.98);
    float sf = uDoy / 365.0 * 4.0 - 0.5;
    float s0 = mod(floor(sf), 4.0), f = fract(sf);
    float s1 = mod(s0 + 1.0, 4.0);
    vec4 a = texture(tex, vec2(cx, (s0 + cy) * 0.25));
    vec4 b = texture(tex, vec2(cx, (s1 + cy) * 0.25));
    return mix(a, b, f);
}
vec4 climAt(vec3 n) { return climSample(uClim, n); }

// Annual mean over the four season bands.
vec4 climAnnual(sampler2D tex, vec3 nRaw) {
    vec3 n = climFuzz(nRaw);
    float lat = asin(clamp(n.z, -1.0, 1.0));
    float lon = atan(n.y, n.x);
    float cx = (lon + 3.14159265) / 6.2831853;
    float cy = clamp((lat + 1.5707963) / 3.14159265, 0.02, 0.98);
    vec4 sum = vec4(0.0);
    for (int sSeason = 0; sSeason < 4; sSeason++)
        sum += texture(tex, vec2(cx, (float(sSeason) + cy) * 0.25));
    return sum * 0.25;
}

// Derived climate (mirrors atmosphere::derivedTempC / derivedMoisture): the
// painted temperatureC / moistureAt retire in favour of these.
float derivedTempC(vec3 n, float h) {
    vec4 c2 = climAnnual(uClim2, n);
    return c2.r - 6.5 * (max(h, 0.0) - c2.b) / 1000.0;
}

float derivedMoist(vec3 n, vec3 w, float h) {
    float rain = climAnnual(uClim, n).g;
    float t = derivedTempC(n, h);
    float pet = max(0.4, 0.11 * (t + 8.0));
    float m = clamp(0.5 * rain / pet, 0.0, 1.0);
    return clamp(m + 0.12 * fbm(w * 5.0 + 31.0, 3, 0.5), 0.0, 1.0);
}

// Seasonal snow cover 0..1 on land: the season's coarse temperature, lapse-
// corrected to the local height, must be freezing, and some precipitation
// must fall to supply the snow.
// Ice factor 0..1 for water surfaces: seasonal local temperature below
// freezing (soft edge). hLocal 0 for the sea.
float iceAt(vec3 n, float hLocal) {
    vec4 c2 = climSample(uClim2, n);
    float tLoc = c2.r - 6.5 * (max(hLocal, 0.0) - c2.b) / 1000.0;
    return smoothstep(-1.0, -4.0, tLoc);
}

float snowCoverAt(vec3 n, float h) {
    vec4 c2 = climSample(uClim2, n);
    float tLoc = c2.r - 6.5 * (max(h, 0.0) - c2.b) / 1000.0;
    return smoothstep(1.0, -3.0, tLoc) * smoothstep(0.01, 0.15, c2.g + climAt(n).g * 0.05);
}

// Cloud cover at n: climatological cloudiness gates a drifting noise field.
// Returns opacity 0..1; rain out-parameter darkens the veil beneath.
float cloudsAt(vec3 n, out float rainV) {
    vec4 cl = climAt(n);
    // wind (m/s) -> angular drift; the local wind warps the noise domain
    vec3 east = normalize(vec3(-n.y, n.x, 0.0));
    vec3 north = normalize(cross(n, east));
    vec3 drift = (east * cl.b + north * cl.w) * (86400.0 / 6371000.0) * uClock;
    float n1 = fbm(n * 9.0 + drift * 0.35, 3, 0.5) * 0.5 + 0.5;
    float n2 = fbm(n * 23.0 + drift * 0.5 + 11.7, 2, 0.5) * 0.5 + 0.5;
    float field = 0.65 * n1 + 0.35 * n2;
    float cov = clamp(cl.r, 0.0, 1.0);
    float density = smoothstep(0.92 - 0.8 * cov, 1.12 - 0.8 * cov, field);
    // rain where the cloud is thick and the climatology is wet
    rainV = density * clamp(cl.g / 6.0, 0.0, 1.0);
    return density * 0.75;
}

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

// ------------------------------------------------------------ climate

// Mean temperature in degrees C: latitude, then a 6.5 C/km lapse rate.
float temperatureC(float lat, float h) {
    return 28.0 - 45.0 * pow(abs(lat) / (PI / 2.0), 1.3) - 6.5 * max(h, 0.0) / 1000.0;
}

// Moisture 0..1: a noise field with subtropical dry bands around +-25 deg.
float moistureAt(vec3 w, float lat) {
    float m = fbm(w * 3.0 + 77.0, 3, 0.5) * 0.5 + 0.5;
    float dryBand = exp(-pow((abs(lat) - 0.42) / 0.16, 2.0));
    return clamp(m - dryBand * 0.45 + 0.08, 0.0, 1.0);
}

// ------------------------------------------------------------ substrate and cover mixtures
// Every point is a mixture: substrate fractions and cover fractions, each
// summing to 1, from soft memberships of the climate variables. "Bare" cover
// is exposed substrate. Mirrored in src/terrain.h — keep in sync.

const int NSUB = 7;   // soil, sand, rock, scree, silt, mud, ice
const int NCOV = 11;  // bare, tundra, taiga, forest, rainforest, grass, steppe, savanna, shrub, marsh, desert

vec3 substrateColor(int s) {
    if (s == 1) return vec3(0.80, 0.72, 0.50); // sand
    if (s == 2) return vec3(0.45, 0.42, 0.38); // rock
    if (s == 3) return vec3(0.52, 0.49, 0.45); // scree
    if (s == 4) return vec3(0.55, 0.48, 0.36); // silt
    if (s == 5) return vec3(0.35, 0.33, 0.25); // mud
    if (s == 6) return vec3(0.94, 0.95, 0.97); // ice
    return vec3(0.45, 0.38, 0.28);             // soil
}

vec3 coverColor(int v) {
    if (v == 1) return vec3(0.55, 0.52, 0.42);  // tundra
    if (v == 2) return vec3(0.10, 0.26, 0.16);  // taiga
    if (v == 3) return vec3(0.12, 0.32, 0.12);  // forest
    if (v == 4) return vec3(0.06, 0.28, 0.10);  // rainforest
    if (v == 5) return vec3(0.36, 0.52, 0.22);  // grassland
    if (v == 6) return vec3(0.62, 0.58, 0.32);  // steppe
    if (v == 7) return vec3(0.60, 0.56, 0.28);  // savanna
    if (v == 8) return vec3(0.50, 0.50, 0.30);  // shrubland
    if (v == 9) return vec3(0.25, 0.40, 0.25);  // marsh
    if (v == 10) return vec3(0.78, 0.66, 0.42); // desert
    return vec3(0.0);                           // bare: drawn with the substrate
}

// Blend a whole mixture toward one member: v = v*(1-t), v[k] += t.
void pull(inout float v[NCOV], int k, float t) {
    for (int i = 0; i < NCOV; i++) v[i] *= 1.0 - t;
    v[k] += t;
}
void pullSub(inout float s[NSUB], int k, float t) {
    for (int i = 0; i < NSUB; i++) s[i] *= 1.0 - t;
    s[k] += t;
}

void substrateMix(float h, float slope, float temp, float moist, float uplift, bool nearRiver, float swamp, out float s[NSUB]) {
    for (int i = 0; i < NSUB; i++) s[i] = 0.0;
    s[0] = 1.0;
    pullSub(s, 1, smoothstep(0.3, 0.18, moist) * smoothstep(2.0, 8.0, temp));
    pullSub(s, 4, nearRiver ? smoothstep(0.03, 0.01, slope) * smoothstep(900.0, 600.0, h) * 0.7 : 0.0);
    pullSub(s, 5, max(smoothstep(0.7, 0.85, moist) * smoothstep(0.025, 0.01, slope) * smoothstep(400.0, 200.0, h),
                      swamp * smoothstep(0.035, 0.015, slope)));
    pullSub(s, 3, smoothstep(0.12, 0.22, slope) * smoothstep(0.2, 0.4, uplift));
    pullSub(s, 2, max(smoothstep(0.28, 0.4, slope), smoothstep(3200.0, 3900.0, h)));
    pullSub(s, 6, smoothstep(-11.0, -16.0, temp + slope * 4.0));
}

// `patchy` is a 0..1 noise that varies tree density within a climate zone.
void coverMix(float h, float slope, float temp, float moist, float uplift, float patchy, float sub[NSUB], out float v[NCOV]) {
    for (int i = 0; i < NCOV; i++) v[i] = 0.0;

    // Trees vs open ground, then each split by climate.
    float tree = smoothstep(0.30, 0.70, moist) * smoothstep(-3.0, 4.0, temp) * (0.45 + 0.55 * patchy);
    float wT = smoothstep(9.0, 3.0, temp);
    float wR = smoothstep(18.0, 23.0, temp) * smoothstep(0.6, 0.72, moist);
    float wF = max(1.0 - wT - wR, 0.0);
    float tn = wT + wR + wF;
    v[2] = tree * wT / tn; v[3] = tree * wF / tn; v[4] = tree * wR / tn;

    float open = 1.0 - tree;
    float oDesert = smoothstep(0.18, 0.06, moist);
    float oSteppe = smoothstep(0.1, 0.22, moist) * smoothstep(0.4, 0.26, moist);
    float oGrassy = smoothstep(0.26, 0.38, moist);
    float oSav = oGrassy * smoothstep(16.0, 20.0, temp) * smoothstep(0.6, 0.45, moist);
    float oGrass = oGrassy - oSav;
    float oShrub = smoothstep(0.1, 0.2, moist) * smoothstep(0.32, 0.2, moist) * (sub[1] + 0.3);
    float on = oDesert + oSteppe + oGrass + oSav + oShrub + 1e-5;
    v[10] += open * oDesert / on; v[6] += open * oSteppe / on; v[5] += open * oGrass / on;
    v[7] += open * oSav / on; v[8] += open * oShrub / on;

    // Overrides, each pulling the whole mixture toward one cover.
    pull(v, 9, sub[5]);                                              // marsh on mud
    pull(v, 1, smoothstep(1.0, -5.0, temp));                         // tundra when cold
    float bare = max(max(sub[2] + sub[3] * 0.6, sub[6]), smoothstep(-8.0, -15.0, temp));
    bare = max(bare, smoothstep(0.1, 0.03, moist) * 0.5);            // dry ground shows through
    pull(v, 0, clamp(bare, 0.0, 1.0));
}

float patchNoise(vec3 w) { return fbm(w * 90.0 + 7.0, 3, 0.5) * 0.5 + 0.5; }

// Rendered colour: substrate mixture underneath, cover mixture on top.
vec3 terrainColor(vec3 w, float h, float slope, float lat, float uplift, bool nearRiver, float swamp,
                  float temp, float moist) {
    float s[NSUB];
    float v[NCOV];
    substrateMix(h, slope, temp, moist, uplift, nearRiver, swamp, s);
    coverMix(h, slope, temp, moist, uplift, patchNoise(w), s, v);
    vec3 base = vec3(0.0);
    for (int i = 0; i < NSUB; i++) base += substrateColor(i) * s[i];
    vec3 c = base * v[0];
    for (int i = 1; i < NCOV; i++) c += coverColor(i) * v[i];
    return c;
}

// Debug: colour of the dominant member.
vec3 debugClassColor(int mode, float h, float slope, float lat, vec3 w, float uplift, bool nearRiver, float swamp,
                     float temp, float moist) {
    float s[NSUB];
    float v[NCOV];
    substrateMix(h, slope, temp, moist, uplift, nearRiver, swamp, s);
    if (mode == 2) {
        int best = 0;
        for (int i = 1; i < NSUB; i++) if (s[i] > s[best]) best = i;
        return substrateColor(best);
    }
    coverMix(h, slope, temp, moist, uplift, patchNoise(w), s, v);
    int best = 0;
    for (int i = 1; i < NCOV; i++) if (v[i] > v[best]) best = i;
    return best == 0 ? vec3(0.15) : coverColor(best);
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
    float slopePhys = length(vec2(dFdx(h), dFdy(h))) / max(uKmPerPixel * 1000.0, 1.0);

    // Water: the sea, a lake surface from the hydrology grid, or a river.
    float waterLevel = 0.0;
    bool isWater = h < 0.0;
    bool isRiver = false;
    if (uHasHydro == 1) {
        // Drawn a little above the spill so flat shores flood irregularly
        // instead of stopping at the grid cell.
        float lakeLevel = lakeLevelAt(n) + 12.0;
        if (lakeLevel > NO_LAKE + 1.0 && h < lakeLevel) { isWater = true; waterLevel = lakeLevel; }
        if (!isWater && h > 0.0 && hydroFetch(hydroCell(n)).a > 0.5 && riverAt(n, w) > 0.0) { isWater = true; isRiver = true; waterLevel = h; }
    }
    // Ponds: tiny lakes below the grid's resolution, where a fine noise
    // peaks on flat, low ground -- and only where the climate's water balance
    // (rain minus evaporation, clim2 alpha) supports standing water. Cold
    // rain-fed plains fill with ponds; deserts hold none.
    bool isPond = false;
    if (!isWater && h > 0.0 && h < 1500.0) {
        float pondField = fbm(w * 700.0 + 91.0, 3, 0.5);
        float flatness = 1.0 - smoothstep(0.0, 0.015, slopePhys);
        float wetness = smoothstep(-0.4, 0.8, climSample(uClim2, n).a);
        if (pondField > 0.42 + 0.20 * (1.0 - wetness) && flatness > 0.3) { isWater = true; isPond = true; waterLevel = h + 3.0; }
    }

    // Relief from screen-space derivatives: one height evaluation per pixel.
    // The surface point in metres is n * (R + h); its screen derivatives span
    // the tangent plane, and their cross product is the normal. Height is
    // exaggerated 3x so relief stays visible from orbit.
    const float R = 6371000.0;
    vec3 dPdx = dFdx(n) * R + n * dFdx(h) * 3.2;
    vec3 dPdy = dFdy(n) * R + n * dFdy(h) * 3.2;
    vec3 shadeN = normalize(cross(dPdx, dPdy));
    if (dot(shadeN, n) < 0.0) shadeN = -shadeN;
    // Physical slope (rise over run) decides rock; the exaggerated normal only shades.
    float slope = clamp(slopePhys * 2.0, 0.0, 1.0);
    if (isWater) { shadeN = n; slope = 0.0; }

    vec3 albedo;
    float upliftHere = plateAt(n).r;
    bool nearRiverHere = uHasHydro == 1 && hydroFetch(hydroCell(n)).a > 0.5;
    if (uDebugMode == 4) {
        // Carrying capacity as a heat ramp; settlements as white dots.
        vec3 c;
        if (isWater) c = vec3(0.05, 0.08, 0.2);
        else {
            float K = texelFetch(uPop, hydroCell(n), 0).r;
            float t2 = clamp(log(max(K, 1.0)) / log(500.0), 0.0, 1.0);
            vec3 warm = mix(vec3(0.9, 0.85, 0.2), vec3(0.85, 0.1, 0.05), smoothstep(0.66, 1.0, t2));
            c = mix(vec3(0.08, 0.08, 0.1), mix(vec3(0.1, 0.3, 0.7), warm, smoothstep(0.33, 0.9, t2)), smoothstep(0.0, 0.4, t2));
        }
        if (settlementNear(n) > 0.0) c = vec3(1.0);
        fragColor = vec4(scaleBarOverlay(c * uDim), 1.0);
        return;
    }
    if (uDebugMode == 2 || uDebugMode == 3) {
        vec3 base = isWater ? vec3(0.05, 0.1, 0.25) : debugClassColor(uDebugMode, h, slopePhys, lat, w, upliftHere, nearRiverHere, 0.55 * smoothstep(0.5, 2.5, climSample(uClim2, n).a), derivedTempC(n, h), derivedMoist(n, w, h));
        fragColor = vec4(scaleBarOverlay(base * uDim), 1.0);
        return;
    }
    if (uDebugMode == 1) {
        // Crust: oceanic blue to continental tan; uplift: red, trench/rift: cyan.
        vec4 pl = plateAt(n);
        vec3 base = mix(vec3(0.15, 0.25, 0.55), vec3(0.65, 0.55, 0.35), pl.g * 0.5 + 0.5);
        if (isWater && !isRiver) base *= 0.6;
        base = mix(base, vec3(0.9, 0.15, 0.1), clamp(pl.r, 0.0, 1.0));
        base = mix(base, vec3(0.1, 0.9, 0.9), clamp(-pl.r, 0.0, 1.0));
        fragColor = vec4(scaleBarOverlay(base * uDim), 1.0);
        return;
    }
    if (uDebugMode == 5) {
        vec4 cl = climAt(n);
        vec3 c = vec3(0.08, 0.07, 0.06);
        if (h <= 0.0) c = vec3(0.05, 0.06, 0.10);
        c += vec3(0.1, 0.5, 0.9) * clamp(cl.g / 8.0, 0.0, 1.0);   // rain
        c += vec3(0.9, 0.9, 0.9) * cl.r * 0.25;                    // cloudiness
        fragColor = vec4(scaleBarOverlay(c * uDim), 1.0);
        return;
    }
    float sp = uHasHydro == 1 ? settlementNear(n) : 0.0;
    float bp = uHasHydro == 1 ? bandNear(n) : 0.0;
    if (sp > 0.0 && !isWater) albedo = vec3(0.55, 0.08, 0.05) * (0.75 + 0.25 * clamp(log(sp) / 11.0, 0.0, 1.0));
    else if (bp > 0.0) albedo = vec3(0.85, 0.55, 0.10); // bands: amber, even mid-crossing
    else if (isRiver) albedo = mix(vec3(0.10, 0.30, 0.48), vec3(0.80, 0.86, 0.92), iceAt(n, h));
    else if (isPond) albedo = mix(vec3(0.14, 0.36, 0.50), vec3(0.80, 0.86, 0.92), iceAt(n, h));
    else if (isWater) {
        albedo = waterColor(waterLevel - h, lat, w);
        // frozen lakes sit at altitude; the sea freezes at its own level
        albedo = mix(albedo, vec3(0.83, 0.88, 0.93), iceAt(n, h > 0.0 ? h : 0.0));
    }
    else {
        float swampV = 0.55 * smoothstep(0.5, 2.5, climSample(uClim2, n).a);
        albedo = terrainColor(w, h, slopePhys, lat, upliftHere, nearRiverHere, swampV,
                              derivedTempC(n, h), derivedMoist(n, w, h));
        albedo = mix(albedo, vec3(0.91, 0.93, 0.96), snowCoverAt(n, h)); // winter snow
    }

    // The sun lives in world space: it circles the globe once per sim day
    // and its declination swings +-23.5 deg over the year, so half the globe
    // is always in night and the poles get their seasons.
    vec3 sun = normalize(uSun);
    float dayF = smoothstep(-0.10, 0.12, dot(n, sun));
    float diff = max(dot(shadeN, sun), 0.0);
    float limb = pow(1.0 - max(dot(n, -dir), 0.0), 3.0);
    vec3 col = mix(albedo * vec3(0.045, 0.055, 0.10), albedo * (0.22 + 0.8 * diff), dayF);
    float rainV;
    float cloudA = cloudsAt(n, rainV);
    // Clouds fade out at close zoom so they never hide the terrain being read.
    cloudA *= smoothstep(0.15, 0.9, uKmPerPixel);
    col = mix(col, col * (1.0 - 0.35 * rainV), cloudA);            // rain veil darkens
    vec3 cloudCol = vec3(1.0) * (0.06 + 0.9 * dayF * max(dot(n, normalize(uSun)), 0.15));
    col = mix(col, cloudCol, cloudA);
    col += vec3(0.3, 0.5, 0.9) * limb * 0.35 * smoothstep(0.0, 0.6, length(uCamPos) - 1.0);

    col = pow(col, vec3(1.0 / 1.6)) * uDim;
    // Awareness zones of selected entities: the 400 km knowledge range,
    // alpha fading with distance like the accuracy does, faint even at centre.
    float aw = 0.0;
    for (int i = 0; i < uAwareCount; i++) {
        float d = acos(clamp(dot(n, uAware[i].xyz), -1.0, 1.0)) * 6371.0;
        aw = max(aw, clamp(1.0 - d / max(uAware[i].w, 1.0), 0.0, 1.0));
    }
    // Violet: a hue the terrain palette never uses, so the zone reads at low alpha.
    col = mix(col, vec3(0.55, 0.20, 1.0), aw * 0.32);

    fragColor = vec4(scaleBarOverlay(col), 1.0);
}
