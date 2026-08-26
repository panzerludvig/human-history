// Daylight and the human activity window (Design/Population.md, Daily
// rhythm). Sleep is a biological constant, light is an economic input, and
// need decides the margin: people work the daylight up to the waking cap,
// and extend into darkness by firelight only as hunger demands. Defined
// ONCE here -- the renderer's sun (main.cpp) and every sim consumer share
// these formulas, so the lit hemisphere on screen and the hours people
// work can never drift apart.
#pragma once
#include <algorithm>
#include <cmath>

namespace daylight {

constexpr double PI_D = 3.14159265358979323846;
constexpr float SLEEP_HOURS = 7.0f;               // biological constant
constexpr float WAKE_HOURS = 24.0f - SLEEP_HOURS; // activity cap: 17 h
constexpr float FIRE_EFFICIENCY = 0.35f;          // working by firelight
constexpr float SUN_ALT_WORK = -0.83f; // horizon incl. refraction: the work day
constexpr float SUN_ALT_MOVE = -6.0f;  // civil twilight: bright enough to walk

// Solar declination, radians. Phase matches the renderer's sun exactly:
// +-23.5 deg peaking at the June 21 solstice (day 171 of the 365-day year).
inline double declination(double t) {
    return 23.5 * PI_D / 180.0 * std::cos(2 * PI_D * (std::fmod(t, 365.0) - 171.0) / 365.0);
}

// The subsolar direction for rendering: declination above, one westward lap
// per day, solar noon at longitude 0 at 12:00.
inline void subsolar(double t, double& dec, double& hourAngle) {
    dec = declination(t);
    hourAngle = PI_D - 2.0 * PI_D * std::fmod(t, 1.0);
}

// Hours per day the sun sits above altDeg at latitude lat (radians).
inline float hoursAbove(float lat, double t, float altDeg) {
    double dec = declination(t);
    double a = altDeg * PI_D / 180.0;
    double cosH = (std::sin(a) - std::sin((double)lat) * std::sin(dec)) /
                  std::max(std::cos((double)lat) * std::cos(dec), 1e-9);
    if (cosH <= -1.0) return 24.0f; // polar day
    if (cosH >= 1.0) return 0.0f;   // polar night
    return (float)(24.0 / PI_D * std::acos(cosH));
}

// The day's work budget in effective hours: daylight at full efficiency up
// to the waking cap, extended into darkness by firelight only as need
// demands (0 content .. 1 desperate). A content settlement stops at
// sunset; a hungry one burns torches. Annual-mean daylight is 12 h at
// every latitude, so the 12-h baseline in the population constants holds.
inline float workHours(float lat, double t, float need) {
    float sun = std::min(hoursAbove(lat, t, SUN_ALT_WORK), WAKE_HOURS);
    return sun + need * FIRE_EFFICIENCY * (WAKE_HOURS - sun);
}

// Hours with enough light to travel (through civil twilight -- arctic
// winters keep a usable glow long after the sun stops rising).
inline float travelHours(float lat, double t) {
    return std::min(hoursAbove(lat, t, SUN_ALT_MOVE), WAKE_HOURS);
}

// Activity-days inside [t0, t1] for a daily window of `hours` centered on
// local solar noon at longitude lon (radians). A span of a day or more
// passes through unchanged -- daily totals already integrate the rhythm --
// while sub-day spans see the diurnal pattern, so the displayed moment is
// honest: nothing moves, gathers, or eats in the dead of night.
inline double activeDays(float lon, double t0, double t1, float hours) {
    if (t1 - t0 >= 1.0) return t1 - t0;
    if (hours <= 0 || t1 <= t0) return 0.0;
    double lonOff = lon / (2 * PI_D); // local solar time offset, in days
    double a = t0 + lonOff, b = t1 + lonOff;
    double half = hours / 48.0; // half-window in days
    double sum = 0;
    for (double d = std::floor(a); d <= std::floor(b) + 1.0; d += 1.0)
        sum += std::max(0.0, std::min(b, d + 0.5 + half) - std::max(a, d + 0.5 - half));
    return sum / (2 * half);
}

} // namespace daylight
