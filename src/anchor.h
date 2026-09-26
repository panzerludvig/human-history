// The anchor the globe shader measures the terrain from: the ground point
// under the camera, and everything a pixel needs to find its own point as an
// exact offset from it. A pixel's surface point as one float vector at
// magnitude ~1 is good to 40 cm of ground, so neighbouring pixels' heights
// were noise at close zoom and the slope had to be measured over a fixed
// 40 m baseline with two extra height evaluations per pixel. Measured from
// the anchor, the offset between two pixels is exact at any zoom and the
// slope comes from screen derivatives. Technical/Globe Viewer.md describes the
// shader side (P3 and Ground in shaders/globe.frag).
#pragma once
#include <cmath>
#include "camera.h"
#include "plates.h"
#include "terrain.h"
#include "world.h"

namespace anchor {

// Uniform values, in the shader's names without the u prefix. Each double
// quantity the shader needs to more than float precision is split as
// hi + lo, hi the float nearest and lo the float nearest the rest.
struct Anchor {
    float point[3];  // uAnchor
    float camRel[3]; // uCamRel
    float c;         // uAnchorC
    float wHi[3], wLo[3];
    float plateHi[2], plateLo[2];
    float earthHi[2], earthLo[2];
};

inline void split(double v, float& hi, float& lo) {
    hi = (float)v;
    lo = (float)(v - (double)hi);
}

// Longitude and latitude of a direction, as the shader's lonLatFrom takes
// them: atan2 on the components, so the point need not be exactly unit.
inline void lonLat(const double g[3], double& lon, double& lat) {
    lon = std::atan2(g[1], g[0]);
    lat = std::atan2(g[2], std::sqrt(g[0] * g[0] + g[1] * g[1]));
}

inline Anchor of(const camera::Camera& cam, const world::World& w) {
    Anchor a{};
    // The anchor is the float-rounded point: every other quantity is derived
    // from that exact value, so the shader's uAnchor + d is on the sphere.
    camera::Vec3 under = camera::sphereDir(cam.lat, cam.lon);
    double g[3] = {(double)(float)under.x, (double)(float)under.y, (double)(float)under.z};
    camera::Vec3 cp = cam.position();
    double cam3[3] = {cp.x, cp.y, cp.z};
    double relSq = 0, gRel = 0, gSq = 0;
    for (int i = 0; i < 3; i++) {
        a.point[i] = (float)g[i];
        a.camRel[i] = (float)(cam3[i] - g[i]);
        // |camera|^2 - 1 for the ray-sphere solve, from the float camRel the
        // shader has: the camera is uAnchor + uCamRel as the shader sees it.
        double rel = a.camRel[i];
        relSq += rel * rel;
        gRel += g[i] * rel;
        gSq += g[i] * g[i];
    }
    a.c = (float)(relSq + 2.0 * gRel + (gSq - 1.0));

    // Noise space: uWorldRot * g + uWorldOff, in double from the float
    // matrix and offset the shader has. Column-major, as terrain::rotate.
    const float* r = w.rot;
    terrain::V3 off = w.terrainOffset();
    double offD[3] = {off.x, off.y, off.z};
    for (int i = 0; i < 3; i++) {
        double v =
            (double)r[i] * g[0] + (double)r[3 + i] * g[1] + (double)r[6 + i] * g[2] + offD[i];
        split(v, a.wHi[i], a.wLo[i]);
    }

    // Texel positions as plateAtTexel and earthAtTexel index them.
    const double PI = camera::PI;
    double lon, lat;
    lonLat(g, lon, lat);
    split((lon + PI) / (2 * PI) * plates::W - 0.5, a.plateHi[0], a.plateLo[0]);
    split((lat + PI / 2) / PI * plates::H - 0.5, a.plateHi[1], a.plateLo[1]);
    const terrain::Template& tp = terrain::TEMPLATE;
    if (tp.active) {
        split((lon + PI) / (2 * PI) * tp.w - 0.5, a.earthHi[0], a.earthLo[0]);
        split((lat + PI / 2) / PI * tp.h - 0.5, a.earthHi[1], a.earthLo[1]);
    }
    return a;
}

} // namespace anchor
