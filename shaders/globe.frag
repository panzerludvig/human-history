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

const float PI = 3.14159265;
const float SEA_LEVEL = 0.0;

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

// Height in [-1, 1]-ish; positive is land. `n` is the unit surface normal.
float terrainHeight(vec3 n, int octaves) {
    // Continents: domain-warped low-frequency noise, biased so ~30% is land.
    vec3 warp = vec3(fbm(n * 1.3 + 11.0, 3, 0.5),
                     fbm(n * 1.3 + 23.0, 3, 0.5),
                     fbm(n * 1.3 + 37.0, 3, 0.5));
    float continent = fbm(n * 1.1 + warp * 0.45, min(octaves, 7), 0.52) - 0.13;

    // Finer detail only where it matters; fades out in deep ocean.
    float detail = fbm(n * 9.0 + 5.0, max(octaves - 3, 1), 0.5);
    float mountains = ridged(n * 4.0 + 2.0, max(octaves - 2, 1));
    float mountainMask = smoothstep(0.02, 0.25, continent) *
                         smoothstep(0.3, 0.7, fbm(n * 2.2 + 41.0, 3, 0.5) * 0.5 + 0.5);

    float h = continent + detail * 0.06 + mountains * mountainMask * 0.5;
    return h;
}

// ------------------------------------------------------------ shading

vec3 terrainColor(vec3 n, float h, float slope, float lat) {
    float absLat = abs(lat);
    // Climate: temperature falls with latitude and altitude; moisture from noise.
    float temp = 1.0 - absLat / (PI / 2.0) - max(h, 0.0) * 1.8;
    float moisture = fbm(n * 3.0 + 77.0, 4, 0.5) * 0.5 + 0.5;
    // Subtropical dry bands around +-25 degrees.
    float dryBand = exp(-pow((absLat - 0.42) / 0.16, 2.0));
    moisture = moisture - dryBand * 0.45;

    if (h < SEA_LEVEL) {
        float depth = clamp(-h * 6.0, 0.0, 1.0);
        vec3 shallow = vec3(0.18, 0.52, 0.68);
        vec3 deep = vec3(0.02, 0.10, 0.30);
        vec3 c = mix(shallow, deep, sqrt(depth));
        if (absLat > 1.28 + 0.06 * noise(n * 6.0)) c = vec3(0.85, 0.9, 0.95); // sea ice
        return c;
    }

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
    c = mix(sand, c, smoothstep(0.0, 0.012, h));
    c = mix(c, rock, smoothstep(0.35, 0.7, slope));
    float snowLine = 0.25 + temp * 0.5;
    c = mix(c, snow, smoothstep(snowLine, snowLine + 0.08, h + (1.0 - temp) * 0.6 - slope * 0.2));
    c = mix(c, snow, smoothstep(0.08, -0.05, temp));
    return c;
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
        fragColor = vec4(bg + vec3(0.25, 0.45, 0.8) * glow * 0.6, 1.0);
        return;
    }
    float t = -b - sqrt(disc);
    vec3 p = uCamPos + dir * t;
    vec3 n = normalize(p);
    float lat = asin(clamp(n.z, -1.0, 1.0));

    float h = terrainHeight(n, uOctaves);

    // Relief: finite-difference gradient in the tangent plane, step ~ one pixel.
    float eps = max(uKmPerPixel / 6371.0, 1e-6);
    vec3 tx = normalize(cross(n, abs(n.z) < 0.99 ? vec3(0, 0, 1) : vec3(1, 0, 0)));
    vec3 ty = cross(n, tx);
    float hx = terrainHeight(normalize(n + tx * eps), uOctaves);
    float hy = terrainHeight(normalize(n + ty * eps), uOctaves);
    // Vertical exaggeration keeps relief visible at every zoom.
    float reliefScale = 0.0025 / eps;
    vec3 gradT = (tx * (hx - h) + ty * (hy - h)) * reliefScale;
    vec3 shadeN = normalize(n - gradT);
    float slope = clamp(length(gradT) * 0.5, 0.0, 1.0);
    if (h < SEA_LEVEL) { shadeN = n; slope = 0.0; }

    vec3 albedo = terrainColor(n, h, slope, lat);

    // Sun fixed relative to the camera so the visible side is always lit.
    vec3 sun = normalize(-uForward * 0.9 + uRight * -0.5 + uUp * 0.6);
    float diff = max(dot(shadeN, sun), 0.0);
    float limb = pow(1.0 - max(dot(n, -dir), 0.0), 3.0);
    vec3 col = albedo * (0.25 + 0.8 * diff);
    col += vec3(0.3, 0.5, 0.9) * limb * 0.35 * smoothstep(0.0, 0.6, length(uCamPos) - 1.0);

    fragColor = vec4(pow(col, vec3(1.0 / 1.6)), 1.0);
}
