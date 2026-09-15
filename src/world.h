// A world: a seed plus the parameters typed into the New World menu, and
// everything derived from them in dependency order -- rotation and offset,
// plates, sea level, hydrology, climate, the rain-fed rivers, settlements.
// Technical/Globe Viewer.md §Menus and worlds says what a world is;
// §Generation feedback says how the build reports its stages, which is the
// progress callback here.
//
// The build is the same for the game and the probes (standards/general.md
// §Verification: same seed, same world), so nothing in this header knows
// about a window.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>
#include "camera.h"
#include "terrain.h"
#include "hydrology.h"
#include "population.h"
#include "technology.h"
#include "atmosphere.h"

namespace world {

// Directory of the running executable: shaders\, data\ and worlds\ sit
// beside it, which is why the path utility lives with the world.
inline std::string exeDir() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s(buf);
    return s.substr(0, s.find_last_of("\\/"));
}

// Generation-stage feedback: the game puts it on the menu status line, a
// probe on stderr. Called with "" when the build is done.
using ProgressFn = void (*)(const char* stage);

// Drainage area above which a cell is a river, both when the rivers are
// first traced and when the painted rain reweights them.
constexpr float RIVER_THRESHOLD_KM2 = 12000.0f;

// atmosphere::build takes a plain function pointer for its year-by-year
// progress, so the caller's ProgressFn reaches it through this. Set for the
// duration of World::build and nowhere else.
inline ProgressFn activeProgress = nullptr;

// A world is a seed plus where the camera was left. The seed rotates and
// offsets the terrain noise so every seed is a different globe.
struct World {
    uint32_t seed = 0;
    bool earth = false; // the seed was "earth": the template globe (see terrain::TEMPLATE)
    float landPercent = 30.0f;
    float concentration = 60.0f; // 0..100: island webs .. one continent
    std::string name;
    float rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // column-major mat3 for GL
    camera::Vec3 offset{};
    terrain::ContinentParams cp{};
    float seaLevel = 0;
    hydrology::Result hydro;
    double simTime = 0; // sim days
    plates::Field plateField;
    population::Field pop;
    technology::WorldState tech;
    atmosphere::Climatology clim;

    void derive() {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> ang(0.0, 2 * camera::PI), off(-2.0, 2.0);
        double a = ang(rng), b = ang(rng), c = ang(rng);
        // Rotation = Rz(a) * Ry(b) * Rx(c), stored column-major.
        double ca = cos(a), sa = sin(a), cb = cos(b), sb = sin(b), cc = cos(c), sc = sin(c);
        double m[3][3] = {
            {ca * cb, ca * sb * sc - sa * cc, ca * sb * cc + sa * sc},
            {sa * cb, sa * sb * sc + ca * cc, sa * sb * cc - ca * sc},
            {-sb, cb * sc, cb * cc},
        };
        for (int col = 0; col < 3; col++)
            for (int row = 0; row < 3; row++) rot[col * 3 + row] = (float)m[row][col];
        offset = {off(rng), off(rng), off(rng)};
        cp = terrain::paramsFor(concentration / 100.0f);
        if (name.empty()) name = earth ? "earth" : "world-" + std::to_string(seed);
    }

    // Everything derived from the seed, in dependency order:
    // plates -> sea level (land %) -> hydrology -> climate -> settlements.
    void build(ProgressFn progress) {
        derive();
        activeProgress = progress;
        terrain::V3 off = {(float)offset.x, (float)offset.y, (float)offset.z};
        terrain::TEMPLATE.active = earth;
        if (earth && terrain::TEMPLATE.elev.empty() &&
            !terrain::loadTemplate(exeDir() + "\\data\\earth.bin")) {
            progress("data\\earth.bin is missing: generating a random world instead");
            earth = false;
            terrain::TEMPLATE.active = false;
        }
        progress("Shaping tectonic plates...");
        plateField = plates::build(seed);
        progress("Setting the sea level...");
        seaLevel = terrain::seaLevelFor(landPercent / 100.0f, cp, rot, off, plateField);
        progress("Tracing rivers and lakes...");
        hydro = hydrology::build(cp, seaLevel, rot, off, RIVER_THRESHOLD_KM2, plateField);
        clim = atmosphere::build(cp, seaLevel, rot, off, plateField, hydro, false,
                                 [](int day, int total) {
                                     char b[80];
                                     snprintf(b, sizeof b, "Simulating climate... year %d of %d",
                                              day / 365 + 1, (total + 364) / 365);
                                     activeProgress(b);
                                 });
        progress("Watering rivers from the rain...");
        {
            std::vector<float> annual(atmosphere::W * atmosphere::H, 0.0f);
            std::vector<float> annualT(atmosphere::W * atmosphere::H, 0.0f);
            for (int i = 0; i < atmosphere::W * atmosphere::H; i++)
                for (int se = 0; se < atmosphere::SEASONS; se++) {
                    annual[i] += clim.rainMmDay[se * atmosphere::W * atmosphere::H + i] /
                                 atmosphere::SEASONS;
                    annualT[i] +=
                        clim.meanT[se * atmosphere::W * atmosphere::H + i] / atmosphere::SEASONS;
                }
            hydrology::reweight(hydro, annual, annualT, atmosphere::W, atmosphere::H,
                                RIVER_THRESHOLD_KM2);
        }
        progress("Placing settlements...");
        pop = population::build(cp, seaLevel, rot, off, plateField, hydro, &clim);
        technology::init(pop, tech, seed, simTime);
        progress("");
        activeProgress = nullptr;
    }
};

} // namespace world
