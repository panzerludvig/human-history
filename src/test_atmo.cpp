// Standalone atmosphere test: build a world's terrain pipeline, run the
// atmosphere generator, and dump climatology maps as BMPs for inspection.
// cl /O2 /EHsc /std:c++17 src\test_atmo.cpp /Fe:build\test_atmo.exe
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include "terrain.h"
#include "hydrology.h"
#include "atmosphere.h"

static void saveBmp(const std::string& path, int W, int H, const std::vector<unsigned char>& rgb) {
    int rowPad = (4 - (W * 3) % 4) % 4, stride = W * 3 + rowPad;
    unsigned int imgSize = stride * H, fileSize = 54 + imgSize;
    unsigned char hdr[54] = {'B', 'M'};
    *(unsigned int*)(hdr + 2) = fileSize;
    *(unsigned int*)(hdr + 10) = 54;
    *(unsigned int*)(hdr + 14) = 40;
    *(int*)(hdr + 18) = W;
    *(int*)(hdr + 22) = -H; // top-down
    *(unsigned short*)(hdr + 26) = 1;
    *(unsigned short*)(hdr + 28) = 24;
    *(unsigned int*)(hdr + 34) = imgSize;
    std::ofstream f(path, std::ios::binary);
    f.write((char*)hdr, 54);
    unsigned char pad[4] = {};
    for (int y = 0; y < H; y++) {
        f.write((const char*)&rgb[y * W * 3], W * 3);
        f.write((char*)pad, rowPad);
    }
}

// value in [0,1] -> blue-white-red
static void tempColor(double t, unsigned char* p) {
    t = std::clamp(t, 0.0, 1.0);
    double r = t < 0.5 ? 2 * t : 1.0, b = t < 0.5 ? 1.0 : 2 * (1 - t);
    double g = 1.0 - 2.0 * std::fabs(t - 0.5);
    p[2] = (unsigned char)(r * 255);
    p[1] = (unsigned char)(g * 230);
    p[0] = (unsigned char)(b * 255);
}

int main(int argc, char** argv) {
    uint32_t seed = argc >= 2 ? (uint32_t)strtoul(argv[1], nullptr, 10) : 7;
    float landPct = 30.0f, conc = 60.0f;
    std::string out = argc >= 3 ? argv[2] : ".";

    // Same derivation as World::build.
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> ang(0.0, 2 * 3.14159265358979), off(-2.0, 2.0);
    double a = ang(rng), b = ang(rng), cgl = ang(rng);
    double ca = cos(a), sa = sin(a), cb = cos(b), sb = sin(b), cc = cos(cgl), sc = sin(cgl);
    double mm[3][3] = {
        {ca * cb, ca * sb * sc - sa * cc, ca * sb * cc + sa * sc},
        {sa * cb, sa * sb * sc + ca * cc, sa * sb * cc - ca * sc},
        {-sb, cb * sc, cb * cc},
    };
    float rot[9];
    for (int col = 0; col < 3; col++)
        for (int row = 0; row < 3; row++) rot[col * 3 + row] = (float)mm[row][col];
    terrain::V3 offset = {(float)off(rng), (float)off(rng), (float)off(rng)};
    terrain::ContinentParams cp = terrain::paramsFor(conc / 100.0f);

    fprintf(stderr, "building plates/sea/hydrology...\n");
    plates::Field pf = plates::build(seed);
    float seaLevel = terrain::seaLevelFor(landPct / 100.0f, cp, rot, offset, pf);
    hydrology::Result hy = hydrology::build(cp, seaLevel, rot, offset, 12000.0f, pf);

    fprintf(stderr, "running atmosphere...\n");
    atmosphere::Climatology c = atmosphere::build(cp, seaLevel, rot, offset, pf, hy, true);

    const int W = atmosphere::W, H = atmosphere::H;
    // land mask for context (thin coast lines in the maps)
    atmosphere::Model m;
    m.init(cp, seaLevel, rot, offset, pf, hy);

    auto dump = [&](const char* name, int season, const std::vector<float>& v, double lo, double hi,
                    bool isTemp) {
        std::vector<unsigned char> img(W * H * 3);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int i = y * W + x;
                double t = (v[season * W * H + i] - lo) / (hi - lo);
                unsigned char* p = &img[i * 3];
                if (isTemp) tempColor(t, p);
                else {
                    t = std::clamp(t, 0.0, 1.0);
                    p[0] = (unsigned char)(40 + 215 * t);  // blue channel rises with value
                    p[1] = (unsigned char)(30 + 120 * t);
                    p[2] = (unsigned char)(20 + 40 * t);
                }
                // coastline: land cells adjacent to water get darkened
                if (!m.water[i]) {
                    bool coast = false;
                    for (int d = -1; d <= 1 && !coast; d += 2)
                        if (m.water[y * W + atmosphere::wrapX(x + d)] ||
                            m.water[std::clamp(y + d, 0, H - 1) * W + x])
                            coast = true;
                    if (coast) { p[0] = p[1] = p[2] = 0; }
                }
            }
        char path[512];
        snprintf(path, sizeof path, "%s/%s.bmp", out.c_str(), name);
        saveBmp(path, W, H, img);
        fprintf(stderr, "wrote %s\n", path);
    };

    // annual means into season slot workspace
    std::vector<float> annT(W * H, 0), annR(W * H, 0), annWind(atmosphere::SEASONS * W * H, 0);
    for (int i = 0; i < W * H; i++) {
        for (int s = 0; s < atmosphere::SEASONS; s++) {
            annT[i] += c.meanT[s * W * H + i] / atmosphere::SEASONS;
            annR[i] += c.rainMmDay[s * W * H + i] / atmosphere::SEASONS;
            float u = c.windU[s * W * H + i], v = c.windV[s * W * H + i];
            annWind[s * W * H + i] = std::sqrt(u * u + v * v);
        }
    }
    std::vector<float> ann4T(atmosphere::SEASONS * W * H), ann4R(atmosphere::SEASONS * W * H);
    std::copy(annT.begin(), annT.end(), ann4T.begin());
    std::copy(annR.begin(), annR.end(), ann4R.begin());

    dump("t_annual", 0, ann4T, -40, 40, true);
    dump("t_djf", 0, c.meanT, -40, 40, true);
    dump("t_jja", 2, c.meanT, -40, 40, true);
    dump("rain_annual", 0, ann4R, 0, 8, false);
    dump("rain_djf", 0, c.rainMmDay, 0, 8, false);
    dump("rain_jja", 2, c.rainMmDay, 0, 8, false);
    dump("wind_djf", 0, annWind, 0, 20, true);
    dump("cloud_annual", 0, c.cloud, 0, 1, false);
    dump("diurnal_jja", 2, c.diurnal, 0, 20, true);

    // zonal means to stderr for quick sanity
    fprintf(stderr, "\nzonal annual means (lat, T, rain mm/day, u m/s):\n");
    for (int y = 0; y < H; y += 8) {
        double T = 0, R = 0, U = 0;
        for (int x = 0; x < W; x++) {
            T += annT[y * W + x];
            R += annR[y * W + x];
            for (int s = 0; s < 4; s++) U += c.windU[s * W * H + y * W + x] / 4.0;
        }
        printf("lat %+5.1f  T %6.1f  rain %5.2f  u %+5.1f\n",
               (((y + 0.5) / H) - 0.5) * 180.0, T / W, R / W, U / W);
        fflush(stdout);
    }
    return 0;
}
