// The constants more than one module needs, each defined once, so two places
// cannot disagree about pi or the size of the Earth (standards/cpp.md
// §Numbers, standards/general.md §Names carry units). A value needed in
// another unit or type is derived here from the one definition, never written
// out again. The shader receives the same values from main.cpp
// (globeConstants), as Technical/Globe Viewer.md describes.
//
// Constants that belong to one model stay in that model's header; this file
// holds only what several share. It includes nothing, so any header can
// include it.
#pragma once

namespace constants {

constexpr double PI = 3.14159265358979323846;
constexpr float PI_F = (float)PI;

// The Earth as a sphere of the mean radius. Every world, generated or the
// Earth template, is this size.
constexpr double EARTH_RADIUS_KM = 6371.0;
constexpr float EARTH_RADIUS_KM_F = (float)EARTH_RADIUS_KM;
constexpr double EARTH_RADIUS_M = EARTH_RADIUS_KM * 1000.0;

// Temperature falls with height at the standard environmental lapse rate.
// The model does not resolve the atmosphere's vertical structure, so this one
// rate stands in for it wherever a temperature is moved between elevations.
constexpr double LAPSE_K_PER_KM = 6.5;
constexpr float LAPSE_K_PER_KM_F = (float)LAPSE_K_PER_KM;

// The calendar: 365-day years, no leap days. Sim time is in days.
constexpr int DAYS_PER_YEAR_INT = 365;
constexpr double DAYS_PER_YEAR = DAYS_PER_YEAR_INT;
constexpr float DAYS_PER_YEAR_F = (float)DAYS_PER_YEAR_INT;

// "Never" as a sim day: what a clock holds when nothing is due. Every real
// sim day is far below it, so a time is due at some point exactly when it is
// below NEVER_DAY; a never plus any real span stays at or above it.
constexpr double NEVER_DAY = 1e18;

} // namespace constants
