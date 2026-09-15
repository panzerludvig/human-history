// The camera above the globe and the vector algebra it needs: where it
// sits, what a pixel's ray is, where a surface point lands on the screen,
// and the three ways input moves it -- zoom towards the cursor, drag the
// grabbed ground under the cursor, drag by pixels when off the globe.
// Technical/Globe Viewer.md §Camera describes the model; the zoom rule is in
// that note's opening paragraph.
//
// Geometry is double throughout (standards/cpp.md §Numbers): a camera at
// 10 km altitude over a unit sphere needs the precision, and nothing here
// is a per-cell field.
#pragma once
#include <algorithm>
#include <cmath>

namespace camera {

struct Vec3 {
    double x, y, z;
};
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 normalize(Vec3 a) {
    double l = std::sqrt(dot(a, a));
    return a * (1.0 / l);
}

constexpr double PI = 3.14159265358979323846;
constexpr double EARTH_RADIUS_KM = 6371.0;
constexpr double FOV_V = 45.0 * PI / 180.0; // vertical field of view
constexpr double MIN_SCREEN_WIDTH_KM = 0.1; // max zoom in: this many km across the screen

// Unit vector on the sphere for a latitude/longitude (radians). +Z is the north pole.
inline Vec3 sphereDir(double lat, double lon) {
    return {std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat)};
}

// The camera sits above a surface point (lat, lon) at `altitude` (in units of
// the sphere radius, R = 1) and looks straight at the globe's centre.
struct Camera {
    double lat = 0.35, lon = 0.0;
    double altitude = 2.0;
    int width = 1280, height = 720;

    double aspect() const { return (double)width / (double)height; }
    double tanHalfV() const { return std::tan(FOV_V / 2); }

    double minAltitude() const {
        return MIN_SCREEN_WIDTH_KM / EARTH_RADIUS_KM / (2.0 * tanHalfV() * aspect());
    }
    double maxAltitude() const {
        // The full disc fits vertically with a small margin beyond its edge.
        double halfFit = std::min(FOV_V / 2, std::atan(tanHalfV() * aspect()));
        return 1.0 / std::sin(0.85 * halfFit) - 1.0;
    }
    void clampAltitude() { altitude = std::clamp(altitude, minAltitude(), maxAltitude()); }

    Vec3 position() const { return sphereDir(lat, lon) * (1.0 + altitude); }
    Vec3 forward() const { return sphereDir(lat, lon) * -1.0; }
    Vec3 up() const {
        // North-pointing tangent; at the camera's own position this is
        // the derivative of sphereDir with respect to latitude.
        return {-std::sin(lat) * std::cos(lon), -std::sin(lat) * std::sin(lon), std::cos(lat)};
    }
    Vec3 right() const { return normalize(cross(forward(), up())); }

    // Ray through a pixel, returned as a direction in world space.
    Vec3 rayThrough(int px, int py) const {
        double nx = (2.0 * (px + 0.5) / width - 1.0) * tanHalfV() * aspect();
        double ny = (1.0 - 2.0 * (py + 0.5) / height) * tanHalfV();
        return normalize(forward() + right() * nx + up() * ny);
    }

    // Where a pixel's ray hits the unit sphere, if it does.
    bool hitSphere(int px, int py, Vec3& out) const {
        Vec3 o = position(), d = rayThrough(px, py);
        double b = dot(o, d);
        double c = dot(o, o) - 1.0;
        double disc = b * b - c;
        if (disc < 0) return false;
        double t = -b - std::sqrt(disc);
        if (t < 0) return false;
        out = normalize(o + d * t);
        return true;
    }

    // Surface kilometres per screen pixel, used for level-of-detail.
    double kmPerPixel() const { return 2.0 * altitude * EARTH_RADIUS_KM * tanHalfV() / height; }
};

// Zoom towards whatever the cursor is over: the ground under it stays under
// it, so closing in on a place is aiming rather than aiming and then
// correcting. Wheeling out runs the same rule backwards, which slides that
// ground away from the cursor as the view widens. A cursor off the globe or
// outside the window leaves the centre where it is.
inline void zoomAt(Camera& cam, double factor, int px, int py) {
    Vec3 anchor{};
    bool haveAnchor =
        px >= 0 && py >= 0 && px < cam.width && py < cam.height && cam.hitSphere(px, py, anchor);
    cam.altitude *= factor;
    cam.clampAltitude();
    if (!haveAnchor) return;
    // Turn the globe so the anchor comes back under the cursor. The camera
    // has no roll -- its up is always north -- so one turn leaves a little
    // tangential drift; three settle it.
    for (int i = 0; i < 3; i++) {
        Vec3 at;
        if (!cam.hitSphere(px, py, at)) break;
        Vec3 axis = cross(at, anchor);
        double sn = std::sqrt(dot(axis, axis));
        if (sn < 1e-12) break;
        axis = axis * (1.0 / sn);
        double ang = std::atan2(sn, dot(at, anchor));
        Vec3 c = sphereDir(cam.lat, cam.lon);
        // Rodrigues: turn the centre through the same rotation.
        Vec3 turned = c * std::cos(ang) + cross(axis, c) * std::sin(ang) +
                      axis * (dot(axis, c) * (1.0 - std::cos(ang)));
        cam.lat = std::asin(std::clamp(turned.z, -1.0, 1.0));
        cam.lon = std::atan2(turned.y, turned.x);
    }
}

// Where a point on the unit sphere lands on the screen; false when it is
// behind the camera or hidden by the curve of the world.
inline bool projectToScreen(const Camera& cam, Vec3 pw, float& sx, float& sy) {
    Vec3 o = cam.position();
    if (dot(pw, o) < 1.0) return false; // over the horizon
    Vec3 d = pw - o;
    double z = dot(d, cam.forward());
    if (z <= 1e-9) return false;
    double nx = dot(d, cam.right()) / z / (cam.tanHalfV() * cam.aspect());
    double ny = dot(d, cam.up()) / z / cam.tanHalfV();
    sx = (float)((nx + 1.0) * 0.5 * cam.width - 0.5);
    sy = (float)((1.0 - ny) * 0.5 * cam.height - 0.5);
    return true;
}

// A left-button drag in progress: the surface point grabbed at mouse-down,
// if the cursor was on the globe, and where the cursor was last seen.
struct Drag {
    bool anchorValid = false;
    Vec3 anchor{};
    int lastX = 0, lastY = 0;
};

// Move the camera so the cursor, now at (x, y), keeps the grabbed ground
// under it; off the globe, rotate by a pixel-proportional amount instead.
inline void applyDrag(Camera& c, const Drag& drag, int x, int y) {
    Vec3 hit;
    if (drag.anchorValid && c.hitSphere(x, y, hit)) {
        // Rotate the camera so the grabbed surface point follows the cursor.
        double latHit = std::asin(std::clamp(hit.z, -1.0, 1.0));
        double lonHit = std::atan2(hit.y, hit.x);
        double latAnc = std::asin(std::clamp(drag.anchor.z, -1.0, 1.0));
        double lonAnc = std::atan2(drag.anchor.y, drag.anchor.x);
        double dLon = lonHit - lonAnc;
        if (dLon > PI) dLon -= 2 * PI;
        if (dLon < -PI) dLon += 2 * PI;
        c.lon -= dLon;
        c.lat -= latHit - latAnc;
    } else {
        // Cursor is off the globe: rotate by a pixel-proportional amount.
        double radPerPx = std::min(2.0 * c.altitude * c.tanHalfV() / c.height, PI / c.height);
        c.lon -= (x - drag.lastX) * radPerPx;
        c.lat += (y - drag.lastY) * radPerPx;
    }
    c.lat = std::clamp(c.lat, -89.0 * PI / 180, 89.0 * PI / 180);
    while (c.lon > PI) c.lon -= 2 * PI;
    while (c.lon < -PI) c.lon += 2 * PI;
}

} // namespace camera
