// A probe, not part of the game: how high the mountain belts reach, and the
// hypsometry around them, for a range of belt gains. The gain had been set so
// the highest peak reached Everest while the belts stood on a base that was
// itself kilometres too tall; with the base corrected the same target gives a
// different gain, so it is re-derived here rather than left where it was.
//
//   build_terrprobe.bat, then build\terrprobe.exe [seed]
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <vector>
#include "constants.h"
#include "world.h"

int main(int argc, char** argv) {
    world::World globe;
    globe.seed = argc >= 2 ? (uint32_t)strtoul(argv[1], nullptr, 10) : 7;
    globe.concentration = 50.0f; // land 30%, the default
    globe.build(nullptr, world::Stage::SeaLevel);
    const uint32_t seed = globe.seed;
    const float* rot = globe.rot;
    const terrain::V3 offset = globe.terrainOffset();
    const terrain::ContinentParams& cp = globe.cp;
    const plates::Field& pf = globe.plateField;
    const float seaLevel = globe.seaLevel;

    printf("seed %u\nlife:  mean 840 m   >2km 5%%   >4km 1%%   peak 8848\n\n", seed);
    printf("%6s %6s %7s %7s %7s %8s %8s\n", "relief", "gain", "mean", ">2km%", ">4km%", "peak",
           "land%");
    float reliefs[] = {0.45f, 0.40f, 0.36f, 0.32f};
    float gains[] = {1.10f, 1.25f, 1.40f};
    const int GW = hydrology::W, GH = hydrology::H;
    for (float rel : reliefs)
      for (float g : gains) {
        terrain::LAND_RELIEF = rel;
        terrain::RANGE_GAIN = g;
        double peak = -1e9, sum = 0, hi2 = 0, hi4 = 0, nL = 0, nAll = 0, wL = 0, wAll = 0;
        #pragma omp parallel for reduction(+:sum,hi2,hi4,nL,nAll,wL,wAll)
        for (int y = 0; y < GH; y++) {
            double lat = ((y + 0.5) / GH - 0.5) * constants::PI;
            double cw = std::cos(lat);
            for (int x = 0; x < GW; x++) {
                double lon = ((x + 0.5) / GW * 2.0 - 1.0) * constants::PI;
                terrain::V3 n{(float)(cw*std::cos(lon)), (float)(cw*std::sin(lon)), (float)std::sin(lat)};
                terrain::V3 w = terrain::rotate(rot, n) + offset;
                float h = terrain::heightMeters(w, n, cp, seaLevel, 8, pf, rot);
                if (h > peak) {
                    #pragma omp critical
                    peak = std::max(peak, (double)h);
                }
                nAll += 1; wAll += cw;
                if (h <= 0) continue;
                nL += 1; wL += cw; sum += h * cw;
                if (h > 2000) hi2 += cw;
                if (h > 4000) hi4 += cw;
            }
        }
        printf("%6.2f %6.2f %7.0f %7.1f %7.1f %8.0f %8.1f\n", rel, g, sum / std::max(wL, 1.0),
               100 * hi2 / std::max(wL, 1.0), 100 * hi4 / std::max(wL, 1.0), peak,
               100 * wL / std::max(wAll, 1.0));
    }
    return 0;
}
