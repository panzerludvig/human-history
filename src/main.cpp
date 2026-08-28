// Human History — version 1: view the globe, zoom, pan.
// Win32 + OpenGL, no external dependencies. The whole globe is raycast and
// shaded procedurally in shaders/globe.frag; this file owns the window,
// the camera, and input.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <GL/gl.h>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <array>
#include <random>
#include <thread>
#include <atomic>
#include "terrain.h"
#include "hydrology.h"
#include "population.h"
#include "technology.h"
#include "sim.h"
#include "atmosphere.h"

// ---------------------------------------------------------------- GL loading

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_RGBA32F 0x8814
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_TEXTURE3 0x84C3
#define GL_TEXTURE4 0x84C4
#define GL_RG32F 0x8230
#define GL_RG 0x8227
#define GL_CLAMP_TO_EDGE 0x812F

typedef GLuint(APIENTRY* PFNGLCREATESHADERPROC)(GLenum);
typedef void(APIENTRY* PFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void(APIENTRY* PFNGLCOMPILESHADERPROC)(GLuint);
typedef void(APIENTRY* PFNGLGETSHADERIVPROC)(GLuint, GLenum, GLint*);
typedef void(APIENTRY* PFNGLGETSHADERINFOLOGPROC)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef GLuint(APIENTRY* PFNGLCREATEPROGRAMPROC)(void);
typedef void(APIENTRY* PFNGLATTACHSHADERPROC)(GLuint, GLuint);
typedef void(APIENTRY* PFNGLLINKPROGRAMPROC)(GLuint);
typedef void(APIENTRY* PFNGLGETPROGRAMIVPROC)(GLuint, GLenum, GLint*);
typedef void(APIENTRY* PFNGLGETPROGRAMINFOLOGPROC)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void(APIENTRY* PFNGLUSEPROGRAMPROC)(GLuint);
typedef GLint(APIENTRY* PFNGLGETUNIFORMLOCATIONPROC)(GLuint, const GLchar*);
typedef void(APIENTRY* PFNGLUNIFORM1FPROC)(GLint, GLfloat);
typedef void(APIENTRY* PFNGLUNIFORM2FPROC)(GLint, GLfloat, GLfloat);
typedef void(APIENTRY* PFNGLUNIFORM3FPROC)(GLint, GLfloat, GLfloat, GLfloat);
typedef void(APIENTRY* PFNGLUNIFORM4FPROC)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void(APIENTRY* PFNGLUNIFORM1IPROC)(GLint, GLint);
typedef void(APIENTRY* PFNGLUNIFORMMATRIX3FVPROC)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void(APIENTRY* PFNGLACTIVETEXTUREPROC)(GLenum);
typedef void(APIENTRY* PFNGLGENVERTEXARRAYSPROC)(GLsizei, GLuint*);
typedef void(APIENTRY* PFNGLBINDVERTEXARRAYPROC)(GLuint);
typedef BOOL(APIENTRY* PFNWGLSWAPINTERVALEXTPROC)(int);

static PFNGLCREATESHADERPROC glCreateShader;
static PFNGLSHADERSOURCEPROC glShaderSource;
static PFNGLCOMPILESHADERPROC glCompileShader;
static PFNGLGETSHADERIVPROC glGetShaderiv;
static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
static PFNGLCREATEPROGRAMPROC glCreateProgram;
static PFNGLATTACHSHADERPROC glAttachShader;
static PFNGLLINKPROGRAMPROC glLinkProgram;
static PFNGLGETPROGRAMIVPROC glGetProgramiv;
static PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
static PFNGLUSEPROGRAMPROC glUseProgram;
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
static PFNGLUNIFORM1FPROC glUniform1f;
static PFNGLUNIFORM2FPROC glUniform2f;
static PFNGLUNIFORM3FPROC glUniform3f;
static PFNGLUNIFORM4FPROC glUniform4f;
typedef void(APIENTRY* PFNGLUNIFORM4FVPROC)(GLint, GLsizei, const GLfloat*);
static PFNGLUNIFORM4FVPROC glUniform4fv;
static PFNGLUNIFORM1IPROC glUniform1i;
static PFNGLUNIFORMMATRIX3FVPROC glUniformMatrix3fv;
static PFNGLACTIVETEXTUREPROC glActiveTexture;
static PFNGLGENVERTEXARRAYSPROC glGenVertexArrays;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArray;
static PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT;

template <typename T>
static bool load(T& fn, const char* name) {
    fn = (T)wglGetProcAddress(name);
    if (!fn) fprintf(stderr, "missing GL function: %s\n", name);
    return fn != nullptr;
}

static bool loadGL() {
    bool ok = true;
    ok &= load(glCreateShader, "glCreateShader");
    ok &= load(glShaderSource, "glShaderSource");
    ok &= load(glCompileShader, "glCompileShader");
    ok &= load(glGetShaderiv, "glGetShaderiv");
    ok &= load(glGetShaderInfoLog, "glGetShaderInfoLog");
    ok &= load(glCreateProgram, "glCreateProgram");
    ok &= load(glAttachShader, "glAttachShader");
    ok &= load(glLinkProgram, "glLinkProgram");
    ok &= load(glGetProgramiv, "glGetProgramiv");
    ok &= load(glGetProgramInfoLog, "glGetProgramInfoLog");
    ok &= load(glUseProgram, "glUseProgram");
    ok &= load(glGetUniformLocation, "glGetUniformLocation");
    ok &= load(glUniform1f, "glUniform1f");
    ok &= load(glUniform2f, "glUniform2f");
    ok &= load(glUniform3f, "glUniform3f");
    ok &= load(glUniform4f, "glUniform4f");
    ok &= load(glUniform4fv, "glUniform4fv");
    ok &= load(glUniform1i, "glUniform1i");
    ok &= load(glUniformMatrix3fv, "glUniformMatrix3fv");
    ok &= load(glActiveTexture, "glActiveTexture");
    ok &= load(glGenVertexArrays, "glGenVertexArrays");
    ok &= load(glBindVertexArray, "glBindVertexArray");
    load(wglSwapIntervalEXT, "wglSwapIntervalEXT"); // optional
    return ok;
}

// ---------------------------------------------------------------- math

struct Vec3 {
    double x, y, z;
};
static Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
static double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static Vec3 normalize(Vec3 a) { double l = std::sqrt(dot(a, a)); return a * (1.0 / l); }

static const double PI = 3.14159265358979323846;
static const double EARTH_RADIUS_KM = 6371.0;
static const double FOV_V = 45.0 * PI / 180.0; // vertical field of view
static const double MIN_SCREEN_WIDTH_KM = 10.0; // max zoom in: this many km across the screen

// Unit vector on the sphere for a latitude/longitude (radians). +Z is the north pole.
static Vec3 sphereDir(double lat, double lon) {
    return {std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat)};
}

// ---------------------------------------------------------------- camera

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

// ---------------------------------------------------------------- shaders

static std::string exeDir() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s(buf);
    return s.substr(0, s.find_last_of("\\/"));
}

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        fprintf(stderr, "cannot read %s\n", path.c_str());
        return "";
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static GLuint compile(GLenum type, const std::string& src, const char* label) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        fprintf(stderr, "%s compile error:\n%s\n", label, log);
        return 0;
    }
    return s;
}

static GLuint buildProgram() {
    std::string dir = exeDir() + "\\shaders\\";
    GLuint vs = compile(GL_VERTEX_SHADER, readFile(dir + "globe.vert"), "vertex");
    GLuint fs = compile(GL_FRAGMENT_SHADER, readFile(dir + "globe.frag"), "fragment");
    if (!vs || !fs) return 0;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(prog, sizeof log, nullptr, log);
        fprintf(stderr, "link error:\n%s\n", log);
        return 0;
    }
    return prog;
}

// ---------------------------------------------------------------- world

// Generation-stage feedback on the menu status line. The build runs on the
// UI thread, so the label is repainted synchronously.
static void buildProgress(const char* stage);

// A world is a seed plus where the camera was left. The seed rotates and
// offsets the terrain noise so every seed is a different globe.
struct World {
    uint32_t seed = 0;
    float landPercent = 30.0f;
    float concentration = 60.0f; // 0..100: island webs .. one continent
    std::string name;
    float rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // column-major mat3 for GL
    Vec3 offset{};
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
        std::uniform_real_distribution<double> ang(0.0, 2 * PI), off(-2.0, 2.0);
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
        if (name.empty()) name = "world-" + std::to_string(seed);
    }

    // Everything derived from the seed, in dependency order:
    // plates -> sea level (land %) -> hydrology.
    void build() {
        derive();
        terrain::V3 off = {(float)offset.x, (float)offset.y, (float)offset.z};
        buildProgress("Shaping tectonic plates...");
        plateField = plates::build(seed);
        buildProgress("Setting the sea level...");
        seaLevel = terrain::seaLevelFor(landPercent / 100.0f, cp, rot, off, plateField);
        buildProgress("Tracing rivers and lakes...");
        hydro = hydrology::build(cp, seaLevel, rot, off, /*riverThresholdKm2=*/12000.0f, plateField);
        clim = atmosphere::build(cp, seaLevel, rot, off, plateField, hydro, false,
                                 [](int day, int total) {
                                     char b[80];
                                     snprintf(b, sizeof b, "Simulating climate... year %d of %d",
                                              day / 365 + 1, (total + 364) / 365);
                                     buildProgress(b);
                                 });
        buildProgress("Watering rivers from the rain...");
        {
            std::vector<float> annual(atmosphere::W * atmosphere::H, 0.0f);
            std::vector<float> annualT(atmosphere::W * atmosphere::H, 0.0f);
            for (int i = 0; i < atmosphere::W * atmosphere::H; i++)
                for (int se = 0; se < atmosphere::SEASONS; se++) {
                    annual[i] += clim.rainMmDay[se * atmosphere::W * atmosphere::H + i] /
                                 atmosphere::SEASONS;
                    annualT[i] += clim.meanT[se * atmosphere::W * atmosphere::H + i] /
                                  atmosphere::SEASONS;
                }
            hydrology::reweight(hydro, annual, annualT, atmosphere::W, atmosphere::H, 12000.0f);
        }
        buildProgress("Placing settlements...");
        pop = population::build(cp, seaLevel, rot, off, plateField, hydro, &clim);
        technology::init(pop, tech, seed, simTime);
        buildProgress("");
    }
};

static std::string worldsDir() { return exeDir() + "\\worlds"; }

static bool saveWorld(const World& w, const Camera& c) {
    CreateDirectoryA(worldsDir().c_str(), nullptr);
    std::ofstream f(worldsDir() + "\\" + w.name + ".ibw");
    if (!f) return false;
    f.precision(17);
    f << "version 12\n";
    f << "seed " << w.seed << "\n";
    f << "time " << w.simTime << "\n";
    f << "land " << w.landPercent << "\n";
    f << "concentration " << w.concentration << "\n";
    f << "lat " << c.lat << "\n";
    f << "lon " << c.lon << "\n";
    f << "altitude " << c.altitude << "\n";
    for (const population::Settlement& s : w.pop.settlements) {
        f << "settlement " << s.cell << " " << s.P << " " << s.R << " ";
        for (int t = 0; t < population::NTECH; t++)
            f << (int)s.tech[t].aware << " " << (int)s.tech[t].practising << " "
              << s.tech[t].practiceT << " ";
        f << s.S << " " << s.scarceSince << " " << s.founded << " " << s.herd << " "
          << s.granaries << " " << s.buildWork << " " << s.fillLo << " " << s.fillHi << " "
          << s.cycleT << " " << s.hungrySince << " " << s.granNeedYrs << " " << s.id << " " << s.starvedYr << "\n";
    }
    for (const population::Band& b : w.pop.bands) {
        f << "band " << b.id << " " << b.px << " " << b.py << " " << b.pz << " " << b.P << " " << b.S << " "
          << b.targetCell << " " << (int)b.resting << " " << b.restStart;
        for (int t = 0; t < population::NTECH; t++)
            f << " " << (int)b.tech[t].aware << " " << (int)b.tech[t].practising << " "
              << b.tech[t].practiceT;
        f << "\n";
    }
    f << "nextsid " << w.pop.nextSettlementId << "\n";
    // Land memory and ruins: where people have lived and left.
    for (const auto& kv : w.pop.scars)
        f << "scar " << kv.first << " " << kv.second.R << " " << kv.second.t << "\n";
    for (const population::Field::Ruin& r : w.pop.ruins)
        f << "ruin " << r.cell << " " << r.abandoned << "\n";
    // Regional game pools: only the dented ones (the rest reload as 1).
    for (size_t r = 0; r < w.pop.gameG.size(); r++)
        if (w.pop.gameG[r] < 0.9999f) f << "game " << r << " " << w.pop.gameG[r] << "\n";
    f << "techrng " << w.tech.rng << "\n";
    return (bool)f;
}

static bool loadWorld(const std::string& name, World& w, Camera& c) {
    std::ifstream f(worldsDir() + "\\" + name + ".ibw");
    if (!f) return false;
    w = World{};
    w.name = name;
    std::vector<std::array<double, 25>> savedSettlements;
    std::vector<std::array<double, 18>> savedBands;
    std::vector<std::pair<size_t, double>> savedGame;
    std::vector<std::array<double, 3>> savedScars;
    std::vector<std::pair<int, double>> savedRuins;
    double savedTime = 0;
    int version = 1;
    uint64_t savedTechRng = 0;
    std::string key;
    while (f >> key) {
        if (key == "version") f >> version;
        else if (key == "seed") f >> w.seed;
        else if (key == "time") f >> savedTime;
        else if (key == "techrng") f >> savedTechRng;
        else if (key == "settlement") {
            // layout: cell P R [aware practising practiceT]xNTECH S scarce founded
            // herd; v8 adds granaries buildWork fillLo fillHi cycleT (and a third
            // tech triple); v9 adds hungrySince granNeedYrs. Unified slots: tech
            // at 3..11, the rest from 12.
            std::array<double, 25> sv{};
            sv[13] = -1; // scarceSince default
            sv[18] = 2;  // fillLo default (no observation yet)
            sv[19] = -1; // fillHi default
            sv[21] = -1; // hungrySince default
            f >> sv[0] >> sv[1] >> sv[2];
            if (version >= 7) {
                int nt = version >= 8 ? 3 : 2;
                for (int t = 0; t < nt * 3; t++) f >> sv[3 + t];
                f >> sv[12] >> sv[13] >> sv[14] >> sv[15];
                if (version >= 8) f >> sv[16] >> sv[17] >> sv[18] >> sv[19] >> sv[20];
                if (version >= 9) f >> sv[21] >> sv[22];
                if (version >= 11) f >> sv[23];
                if (version >= 12) f >> sv[24];
            } else {
                if (version >= 3) f >> sv[3] >> sv[4] >> sv[5];
                if (version >= 4) f >> sv[12] >> sv[13];
                else sv[12] = 0.5 * population::CAP_DAYS_SETTLED * sv[1];
                if (version >= 6) f >> sv[14];
            }
            savedSettlements.push_back(sv);
        }
        else if (key == "band") {
            std::array<double, 18> bv{};
            if (version >= 7) {
                int n = version >= 8 ? 18 : 15;
                for (int i = 0; i < n; i++) f >> bv[i];
            } else {
                int i0 = version >= 5 ? 0 : 1;
                for (int i = i0; i < 12; i++) f >> bv[i];
            }
            savedBands.push_back(bv);
        }
        else if (key == "nextsid") f >> w.pop.nextSettlementId;
        else if (key == "scar") {
            double cell, R, t;
            f >> cell >> R >> t;
            savedScars.push_back({cell, R, t});
        }
        else if (key == "ruin") {
            int cell;
            double t;
            f >> cell >> t;
            savedRuins.push_back({cell, t});
        }
        else if (key == "game") {
            size_t r;
            double g;
            f >> r >> g;
            savedGame.push_back({r, g});
        }
        else if (key == "land") f >> w.landPercent;
        else if (key == "concentration") f >> w.concentration;
        else if (key == "lat") f >> c.lat;
        else if (key == "lon") f >> c.lon;
        else if (key == "altitude") f >> c.altitude;
        else { std::string skip; f >> skip; }
    }
    w.build();
    // Restore the saved population on top of the regenerated field; local
    // properties come from the per-cell maps, so founded settlements restore
    // the same way as original ones.
    if (!savedSettlements.empty()) {
        w.pop.settlements.clear();
        w.pop.bands.clear();
        std::fill(w.pop.settlementAt.begin(), w.pop.settlementAt.end(), -1);
        for (auto& sv : savedSettlements) {
            int cell = (int)sv[0];
            if (cell < 0 || cell >= population::W * population::H) continue;
            population::Settlement st{cell, 0, false, (float)sv[1], (float)sv[2], savedTime, savedTime};
            st.kFoodP = w.pop.kFoodPMap[cell];
            st.kWater = w.pop.kWaterMap[cell];
            st.sFarm = w.pop.sFarmMap[cell];
            st.pasture = w.pop.pastureMap[cell];
            for (int t = 0; t < population::NTECH; t++) {
                st.tech[t].aware = sv[3 + t * 3] > 0.5;
                st.tech[t].practising = sv[4 + t * 3] > 0.5;
                st.tech[t].practiceT = sv[5 + t * 3];
            }
            st.S = (float)sv[12];
            st.scarceSince = sv[13];
            st.founded = sv[14];
            st.herd = (float)sv[15];
            st.granaries = (float)sv[16];
            st.buildWork = (float)sv[17];
            st.fillLo = (float)sv[18];
            st.fillHi = (float)sv[19];
            st.cycleT = version >= 8 ? sv[20] : savedTime;
            st.hungrySince = sv[21];
            st.granNeedYrs = (float)sv[22];
            st.starvedYr = (float)sv[24];
            st.buildMat = w.pop.buildMatMap[cell];
            st.kGame = w.pop.kGameMap[cell];
            st.gRegion = population::gameRegion(cell);
            st.id = sv[23] > 0 ? (uint32_t)sv[23] : w.pop.nextSettlementId++;
            w.pop.nextSettlementId = std::max(w.pop.nextSettlementId, st.id + 1);
            if (st.tech[population::TECH_HUSBANDRY].practising && st.herd <= 0)
                st.herd = technology::HERD_SEED;
            atmosphere::seasonProfile(w.clim, sim::cellCentre(cell),
                                      std::max(w.hydro.heightM[cell], 0.0f), st.tSeason,
                                      st.meanF, st.meanG2);
            w.pop.settlementAt[cell] = (int)w.pop.settlements.size();
            w.pop.settlements.push_back(st);
        }
        for (auto& bv : savedBands) {
            population::Band b{};
            b.id = (uint32_t)bv[0];
            if (!b.id) b.id = w.pop.nextBandId;
            w.pop.nextBandId = std::max(w.pop.nextBandId, b.id + 1);
            b.px = (float)bv[1]; b.py = (float)bv[2]; b.pz = (float)bv[3];
            b.P = (float)bv[4]; b.S = (float)bv[5];
            b.targetCell = (int)bv[6];
            b.resting = bv[7] > 0.5;
            b.restStart = bv[8];
            for (int t = 0; t < population::NTECH; t++) {
                b.tech[t].aware = bv[9 + t * 3] > 0.5;
                b.tech[t].practising = bv[10 + t * 3] > 0.5;
                b.tech[t].practiceT = bv[11 + t * 3];
            }
            b.t = savedTime;
            b.nextUpdate = savedTime;
            if (b.targetCell >= 0 && b.targetCell < population::W * population::H)
                w.pop.bands.push_back(b);
        }
        population::computeNeighbours(w.pop);
    }
    for (auto& sc : savedScars)
        if (sc[0] >= 0 && sc[0] < population::W * population::H)
            w.pop.scars[(int)sc[0]] = {(float)sc[1], sc[2]};
    for (auto& rn : savedRuins)
        if (rn.first >= 0 && rn.first < population::W * population::H)
            w.pop.ruins.push_back({rn.first, rn.second});
    // Restore the game pools (default pristine), then refresh each
    // settlement's cached health.
    for (auto& [r, g] : savedGame)
        if (r < w.pop.gameG.size()) w.pop.gameG[r] = (float)g;
    w.pop.gameT = savedTime;
    for (population::Settlement& st : w.pop.settlements)
        if (st.kGame > 0) st.gameNow = w.pop.gameG[st.gRegion];
    w.simTime = savedTime;
    if (savedTechRng) w.tech.rng = savedTechRng;
    // Contact draws and the invention clock are exponential (memoryless), so
    // redrawing them on load is statistically exact.
    for (int t = 0; t < population::NTECH; t++) {
        for (int i = 0; i < (int)w.pop.settlements.size(); i++)
            technology::redraw(w.pop, i, w.tech, t, savedTime);
        technology::scheduleInvention(w.pop, w.tech, t, savedTime);
    }
    c.clampAltitude();
    return true;
}

static std::vector<std::string> listWorlds() {
    std::vector<std::string> names;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((worldsDir() + "\\*.ibw").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return names;
    do {
        std::string n = fd.cFileName;
        names.push_back(n.substr(0, n.size() - 4));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end());
    return names;
}

// ---------------------------------------------------------------- app state

enum class Screen { MainMenu, NewWorldMenu, LoadMenu, InGame, PauseMenu };

// Control IDs for the Win32 controls that make up the menus.
enum : int {
    ID_NEW_WORLD = 100, ID_LOAD_WORLD, ID_QUIT,
    ID_LOAD_LIST, ID_LOAD_CONFIRM, ID_LOAD_DELETE, ID_LOAD_BACK,
    ID_SAVE_NAME, ID_SAVE_WORLD, ID_MAIN_MENU, ID_PAUSE_QUIT,
    ID_TITLE, ID_STATUS,
    ID_GEN_SEED_LABEL, ID_GEN_SEED, ID_GEN_RANDOM, ID_GEN_LAND_LABEL, ID_GEN_LAND,
    ID_GEN_CONC_LABEL, ID_GEN_CONC, ID_GEN_HINT, ID_GEN_CREATE, ID_GEN_BACK,
    ID_SCALE_LABEL, ID_TOOLTIP,
    ID_TIME_STEP, ID_TIME_GO, ID_DATE_LABEL,
};

// A detail window for one settlement or band, opened by clicking its marker.
// Settlement panels are tabbed (Environment / Technology / Buildings) and
// custom-painted so tabs and technology rows are clickable; every panel is
// repositioned by dragging anywhere that isn't a click target.
struct Panel {
    HWND wnd = nullptr;
    int kind = 0;       // 0 settlement, 1 band
    uint32_t sid = 0;   // settlement identity (indices shift when one moves away)
    uint32_t bandId = 0;
    int tab = 0;        // 0 environment, 1 technology, 2 buildings
    int techSel = -1;   // selected tech in the tech tab, -1 = overview list
};

struct App {
    Camera cam;
    World world;
    Screen screen = Screen::MainMenu;
    bool dragging = false;
    bool anchorValid = false;
    Vec3 anchor{}; // surface point grabbed at mouse-down
    int lastX = 0, lastY = 0;
    int downX = 0, downY = 0;   // mouse-down spot, to tell a click from a drag
    bool clickMoved = false;
    std::vector<Panel> panels;
    int panelSpawn = 0;         // cascade offset for new panels
    // World generation runs on a worker thread so the window stays live; the
    // menus never render the globe, so the worker owns app.world meanwhile.
    std::thread genThread;
    std::atomic<int> genState{0}; // 0 idle, 1 running, 2 finished
    bool genOk = false;
    int genKind = 0;            // 0 new world, 1 load
    std::string genName;
    GLuint program = 0;
    GLuint hydroTex = 0;
    GLuint plateTex = 0;
    GLuint popTex = 0;
    GLuint climTex = 0;
    GLuint clim2Tex = 0;
    bool running = true;
    std::string shotPath; // when set, save the next rendered frame here (testing)
    int debugMode = 0; // 0 normal, 1 plates, 2 substrate, 3 vegetation
    int octaves = 8;   // current level of detail, shared with the tooltip
    HWND hwnd = nullptr;
    HFONT font = nullptr, titleFont = nullptr;
    HFONT panelFont = nullptr, panelBold = nullptr; // dense panel text
    HBRUSH bgBrush = nullptr;
    HWND panelDrag = nullptr; // panel being dragged, with the grab offset
    POINT panelDragOff{};
    std::vector<std::pair<int, HWND>> controls;
};
static App app;

// Push the plate table to texture unit 1, bilinear so belts are smooth.
static void uploadPlates() {
    glActiveTexture(GL_TEXTURE1);
    if (!app.plateTex) {
        glGenTextures(1, &app.plateTex);
        glBindTexture(GL_TEXTURE_2D, app.plateTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, app.plateTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, plates::W, plates::H, 0, GL_RGBA, GL_FLOAT,
                 app.world.plateField.cells.data());
    glActiveTexture(GL_TEXTURE0);
}

static std::vector<float> popTexData() {
    std::vector<float> d(population::W * population::H * 4, 0.0f);
    const population::Field& pf = app.world.pop;
    for (int i = 0; i < population::W * population::H; i++) d[i * 4] = pf.K[i];
    for (const population::Field::Ruin& r : pf.ruins) d[r.cell * 4 + 1] = -1.0f; // abandoned
    for (const population::Settlement& s : pf.settlements) {
        d[s.cell * 4 + 1] = std::max(s.P, 1.0f);
        d[s.cell * 4 + 3] = s.granaries; // alpha: built granaries at the cell
    }
    for (const population::Band& b : pf.bands) {
        int cell = sim::cellOf({b.px, b.py, b.pz});
        d[cell * 4 + 2] += std::max(b.P, 1.0f);
    }
    return d;
}

static void uploadPopulation() {
    glActiveTexture(GL_TEXTURE2);
    if (!app.popTex) {
        glGenTextures(1, &app.popTex);
        glBindTexture(GL_TEXTURE_2D, app.popTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_2D, app.popTex);
    std::vector<float> d = popTexData();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, population::W, population::H, 0, GL_RGBA, GL_FLOAT, d.data());
    glActiveTexture(GL_TEXTURE0);
}

// Climatology texture: four season bands stacked vertically, RGBA =
// {cloud, rain mm/day, wind u, wind v}.
static void uploadClimatology() {
    glActiveTexture(GL_TEXTURE3);
    if (!app.climTex) {
        glGenTextures(1, &app.climTex);
        glBindTexture(GL_TEXTURE_2D, app.climTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, app.climTex);
    const atmosphere::Climatology& c = app.world.clim;
    int W = atmosphere::W, H = atmosphere::H, S = atmosphere::SEASONS;
    std::vector<float> d(W * H * S * 4);
    for (int i = 0; i < W * H * S; i++) {
        d[i * 4 + 0] = c.cloud[i];
        d[i * 4 + 1] = c.rainMmDay[i];
        d[i * 4 + 2] = c.windU[i];
        d[i * 4 + 3] = c.windV[i];
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, W, H * S, 0, GL_RGBA, GL_FLOAT, d.data());
    // Second climatology texture: seasonal mean temperature, snowfall, and the
    // model's smoothed elevation (so the shader can lapse-correct to local
    // terrain height for snow cover).
    glActiveTexture(GL_TEXTURE4);
    if (!app.clim2Tex) {
        glGenTextures(1, &app.clim2Tex);
        glBindTexture(GL_TEXTURE_2D, app.clim2Tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, app.clim2Tex);
    // Alpha carries the annual water balance (rain - PET, mm/day): the
    // shader's pond density follows it.
    std::vector<float> balance(W * H, 0.0f);
    for (int i = 0; i < W * H; i++) {
        float rain = 0, tC = 0;
        for (int se = 0; se < S; se++) {
            rain += c.rainMmDay[se * W * H + i] / S;
            tC += c.meanT[se * W * H + i] / S;
        }
        balance[i] = rain - hydrology::petMmDay(tC);
    }
    for (int se = 0; se < S; se++)
        for (int i = 0; i < W * H; i++) {
            int si = se * W * H + i;
            d[si * 4 + 0] = c.meanT[si];
            d[si * 4 + 1] = c.snowMmDay[si];
            d[si * 4 + 2] = c.elev.empty() ? 0.0f : c.elev[i];
            d[si * 4 + 3] = balance[i];
        }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, W, H * S, 0, GL_RGBA, GL_FLOAT, d.data());
    glActiveTexture(GL_TEXTURE0);
}

// Push the world's hydrology table to the GPU as one RGBA32F texel per cell.
static void uploadHydrology() {
    uploadPlates();
    uploadPopulation();
    uploadClimatology();
    if (!app.hydroTex) {
        glGenTextures(1, &app.hydroTex);
        glBindTexture(GL_TEXTURE_2D, app.hydroTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_2D, app.hydroTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, hydrology::W, hydrology::H, 0, GL_RGBA, GL_FLOAT,
                 app.world.hydro.cells.data());
}

static void advanceDays(double days);
static void updateDateLabel();
static void closeAllPanels();
static void refreshPanels();

static HWND control(int id) {
    for (auto& c : app.controls)
        if (c.first == id) return c.second;
    return nullptr;
}

static void setStatus(const std::string& s) { SetWindowTextA(control(ID_STATUS), s.c_str()); }

static void buildProgress(const char* stage) {
    fprintf(stderr, "build: %s%c", stage, 10);
    // Skip when the status line is not on screen (e.g. the argv test path).
    HWND st = control(ID_STATUS);
    if (!st || !IsWindowVisible(st)) return;
    SetWindowTextA(st, stage);
    UpdateWindow(st);
}

static void addControl(int id, const char* cls, const char* text, DWORD style) {
    HWND h = CreateWindowA(cls, text, WS_CHILD | style, 0, 0, 10, 10, app.hwnd, (HMENU)(INT_PTR)id,
                           GetModuleHandleA(nullptr), nullptr);
    SendMessageA(h, WM_SETFONT, (WPARAM)(id == ID_TITLE ? app.titleFont : app.font), TRUE);
    app.controls.push_back({id, h});
}

static void createControls() {
    app.font = CreateFontA(24, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    app.titleFont = CreateFontA(56, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    app.panelFont = CreateFontA(18, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    app.panelBold = CreateFontA(18, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    app.bgBrush = CreateSolidBrush(RGB(8, 8, 16));
    addControl(ID_TITLE, "STATIC", "Human History", SS_CENTER);
    addControl(ID_STATUS, "STATIC", "", SS_CENTER);
    addControl(ID_NEW_WORLD, "BUTTON", "New World", BS_PUSHBUTTON);
    addControl(ID_LOAD_WORLD, "BUTTON", "Load World", BS_PUSHBUTTON);
    addControl(ID_QUIT, "BUTTON", "Quit", BS_PUSHBUTTON);
    addControl(ID_LOAD_LIST, "LISTBOX", "", WS_BORDER | WS_VSCROLL | LBS_NOTIFY);
    addControl(ID_LOAD_CONFIRM, "BUTTON", "Load", BS_PUSHBUTTON);
    addControl(ID_LOAD_DELETE, "BUTTON", "Delete", BS_PUSHBUTTON);
    addControl(ID_LOAD_BACK, "BUTTON", "Back", BS_PUSHBUTTON);
    addControl(ID_SAVE_NAME, "EDIT", "", WS_BORDER | ES_AUTOHSCROLL | ES_CENTER);
    SendMessageA(control(ID_SAVE_NAME), EM_SETLIMITTEXT, 64, 0);
    addControl(ID_SAVE_WORLD, "BUTTON", "Save World", BS_PUSHBUTTON);
    addControl(ID_MAIN_MENU, "BUTTON", "Main Menu", BS_PUSHBUTTON);
    addControl(ID_PAUSE_QUIT, "BUTTON", "Quit Game", BS_PUSHBUTTON);
    addControl(ID_GEN_SEED_LABEL, "STATIC", "Seed", SS_RIGHT);
    addControl(ID_GEN_SEED, "EDIT", "", WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER);
    addControl(ID_GEN_RANDOM, "BUTTON", "Random", BS_PUSHBUTTON);
    addControl(ID_GEN_LAND_LABEL, "STATIC", "Land %", SS_RIGHT);
    addControl(ID_GEN_LAND, "EDIT", "", WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER);
    addControl(ID_GEN_CONC_LABEL, "STATIC", "Concentration %", SS_RIGHT);
    addControl(ID_GEN_CONC, "EDIT", "", WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER);
    addControl(ID_GEN_HINT, "STATIC", "Concentration: 0 = island webs and thin strips, 100 = one massive continent", SS_CENTER);
    addControl(ID_GEN_CREATE, "BUTTON", "Generate", BS_PUSHBUTTON);
    addControl(ID_GEN_BACK, "BUTTON", "Back", BS_PUSHBUTTON);
    addControl(ID_SCALE_LABEL, "STATIC", "", SS_LEFT);
    addControl(ID_TOOLTIP, "STATIC", "", SS_LEFT | SS_NOPREFIX);
    addControl(ID_DATE_LABEL, "STATIC", "", SS_RIGHT);
    addControl(ID_TIME_STEP, "COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL);
    addControl(ID_TIME_GO, "BUTTON", "Advance", BS_PUSHBUTTON);
    {
        HWND cb = control(ID_TIME_STEP);
        for (const char* it : {"1 minute", "1 hour", "1 day", "1 month", "1 year", "10 years", "100 years"})
            SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)it);
        SendMessageA(cb, CB_SETCURSEL, 2, 0); // default: 1 day
    }
}

// Map scale bar: a 1/2/5 x 10^n distance whose bar is close to a target
// width, placed in the bottom-left corner with its label above it.
const int SCALE_MARGIN = 24;
struct ScaleBar {
    double km = 0;
    int px = 0;
};
static ScaleBar chooseScale(double kmPerPixel, int targetPx = 160) {
    double raw = kmPerPixel * targetPx;
    double mag = std::pow(10.0, std::floor(std::log10(raw)));
    double best = mag;
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (m * mag <= raw) best = m * mag;
    return {best, (int)std::lround(best / kmPerPixel)};
}
static std::string scaleText(double km) {
    char buf[32];
    if (km >= 1.0) snprintf(buf, sizeof buf, "%g km", km);
    else snprintf(buf, sizeof buf, "%g m", km * 1000.0);
    return buf;
}

// Position and show the controls that belong to the current screen.
static void layoutControls() {
    int W = app.cam.width, H = app.cam.height;
    const int bw = 280, bh = 48, gap = 14;
    int cx = W / 2 - bw / 2;
    for (auto& c : app.controls) ShowWindow(c.second, SW_HIDE);

    auto place = [&](int id, int x, int y, int w, int h) {
        SetWindowPos(control(id), HWND_TOP, x, y, w, h, SWP_SHOWWINDOW);
    };
    auto stack = [&](std::initializer_list<int> ids, int top) {
        int y = top;
        for (int id : ids) {
            place(id, cx, y, bw, bh);
            y += bh + gap;
        }
        return y;
    };

    switch (app.screen) {
    case Screen::MainMenu:
        place(ID_TITLE, 0, H / 4 - 40, W, 70);
        stack({ID_NEW_WORLD, ID_LOAD_WORLD, ID_QUIT}, H / 2 - bh);
        place(ID_STATUS, 0, H - 60, W, 30);
        break;
    case Screen::NewWorldMenu: {
        place(ID_TITLE, 0, H / 8, W, 70);
        const int lw = 200, ew = 200, rh = 34, rgap = 16;
        int x0 = W / 2 - (lw + 12 + ew) / 2;
        int y = H / 4 + 50;
        auto row = [&](int label, int edit, int extra) {
            place(label, x0, y + 4, lw, rh);
            place(edit, x0 + lw + 12, y, ew, rh);
            if (extra) place(extra, x0 + lw + 12 + ew + 12, y - 2, 110, rh + 4);
            y += rh + rgap;
        };
        row(ID_GEN_SEED_LABEL, ID_GEN_SEED, ID_GEN_RANDOM);
        row(ID_GEN_LAND_LABEL, ID_GEN_LAND, 0);
        row(ID_GEN_CONC_LABEL, ID_GEN_CONC, 0);
        place(ID_GEN_HINT, 0, y, W, 30);
        stack({ID_GEN_CREATE, ID_GEN_BACK}, y + 44);
        place(ID_STATUS, 0, H - 60, W, 30);
        break;
    }
    case Screen::LoadMenu: {
        place(ID_TITLE, 0, H / 8, W, 70);
        int listTop = H / 4 + 40, listH = H / 3;
        place(ID_LOAD_LIST, cx, listTop, bw, listH);
        stack({ID_LOAD_CONFIRM, ID_LOAD_DELETE, ID_LOAD_BACK}, listTop + listH + gap);
        place(ID_STATUS, 0, H - 60, W, 30);
        break;
    }
    case Screen::PauseMenu: {
        int top = H / 2 - 2 * (bh + gap);
        place(ID_SAVE_NAME, cx, top, bw, 34);
        int bottom = stack({ID_SAVE_WORLD, ID_MAIN_MENU, ID_PAUSE_QUIT}, top + 34 + gap);
        place(ID_STATUS, cx - 60, bottom, bw + 120, 30);
        break;
    }
    case Screen::InGame: {
        place(ID_SCALE_LABEL, SCALE_MARGIN, H - SCALE_MARGIN - 44, 110, 26);
        // Time stepping, top right: a step-size dropdown and one Advance
        // button. Temporary evaluation tooling: the simulation is paused
        // unless stepped. The dropdown's height is the room its open list
        // gets, not the closed control's height.
        int cw = 130, bw = 100, th = 32, tg = 6;
        place(ID_DATE_LABEL, W - cw - bw - 2 * tg - 160, 16, 150, 24);
        place(ID_TIME_STEP, W - cw - bw - 2 * tg, 12, cw, 220);
        place(ID_TIME_GO, W - bw - tg, 10, bw, th);
        break;
    }
    }
}

static void refreshWorldList() {
    HWND list = control(ID_LOAD_LIST);
    SendMessageA(list, LB_RESETCONTENT, 0, 0);
    for (auto& n : listWorlds()) SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)n.c_str());
    SendMessageA(list, LB_SETCURSEL, 0, 0);
}

// Name of the world currently selected in the load list, or empty.
static std::string selectedWorld() {
    HWND list = control(ID_LOAD_LIST);
    int sel = (int)SendMessageA(list, LB_GETCURSEL, 0, 0);
    if (sel < 0) return "";
    char name[MAX_PATH];
    SendMessageA(list, LB_GETTEXT, sel, (LPARAM)name);
    return name;
}

// Turn whatever was typed into something that is safe as a file name.
static std::string sanitizeName(std::string n) {
    const std::string bad = "\\/:*?\"<>|";
    for (char& ch : n)
        if (bad.find(ch) != std::string::npos || (unsigned char)ch < 32) ch = '_';
    size_t a = n.find_first_not_of(" ."), b = n.find_last_not_of(" .");
    if (a == std::string::npos) return "";
    return n.substr(a, b - a + 1);
}

static void setEditNumber(int id, double v, int decimals = 0) {
    char buf[64];
    snprintf(buf, sizeof buf, "%.*f", decimals, v);
    SetWindowTextA(control(id), buf);
}

static double getEditNumber(int id) {
    char buf[64];
    GetWindowTextA(control(id), buf, sizeof buf);
    return atof(buf);
}

static void fillNewWorldFields(const World& w) {
    setEditNumber(ID_GEN_SEED, (double)w.seed);
    setEditNumber(ID_GEN_LAND, w.landPercent);
    setEditNumber(ID_GEN_CONC, w.concentration);
}

static void setScreen(Screen s) {
    app.screen = s;
    ShowWindow(control(ID_TOOLTIP), SW_HIDE);
    if (s != Screen::InGame) closeAllPanels();
    app.dragging = false;
    if (s == Screen::LoadMenu) refreshWorldList();
    if (s == Screen::NewWorldMenu) fillNewWorldFields(app.world);
    if (s == Screen::PauseMenu) SetWindowTextA(control(ID_SAVE_NAME), app.world.name.c_str());
    layoutControls();
    if (s == Screen::InGame) { updateDateLabel(); SetFocus(app.hwnd); }
    if (s == Screen::PauseMenu) {
        HWND edit = control(ID_SAVE_NAME);
        SetFocus(edit);
        SendMessageA(edit, EM_SETSEL, 0, -1);
    }
}

static uint32_t randomSeed() { return (uint32_t)std::random_device{}(); }

static void openNewWorldMenu() {
    app.world = World{};
    app.world.seed = randomSeed();
    setStatus("");
    setScreen(Screen::NewWorldMenu);
}

static void generateWorld() {
    World w;
    w.seed = (uint32_t)std::clamp(getEditNumber(ID_GEN_SEED), 0.0, 4294967295.0);
    w.landPercent = (float)std::clamp(getEditNumber(ID_GEN_LAND), 0.0, 100.0);
    w.concentration = (float)std::clamp(getEditNumber(ID_GEN_CONC), 0.0, 100.0);
    app.world = w;
    app.genKind = 0;
    app.genState = 1;
    app.genThread = std::thread([] {
        app.world.build();
        app.genOk = true;
        app.genState = 2;
    });
}

// Runs on the main (GL) thread once the worker finishes: textures and the
// screen switch happen here.
static void finishGeneration() {
    app.genThread.join();
    app.genState = 0;
    if (!app.genOk) {
        setStatus("Could not load " + app.genName);
        return;
    }
    uploadHydrology();
    if (app.genKind == 0) {
        app.cam.lat = 0.35;
        app.cam.lon = 0.0;
        app.cam.altitude = app.cam.maxAltitude();
    }
    setStatus("");
    setScreen(Screen::InGame);
}

static void onCommand(int id) {
    if (app.genState != 0) return; // generation in progress: only the OS window moves
    switch (id) {
    case ID_NEW_WORLD: openNewWorldMenu(); break;
    case ID_GEN_CREATE: generateWorld(); break;
    case ID_GEN_BACK: setScreen(Screen::MainMenu); break;
    case ID_GEN_RANDOM: setEditNumber(ID_GEN_SEED, (double)randomSeed()); break;
    case ID_LOAD_WORLD:
        setStatus("");
        setScreen(Screen::LoadMenu);
        break;
    case ID_QUIT:
    case ID_PAUSE_QUIT: app.running = false; break;
    case ID_LOAD_BACK: setScreen(Screen::MainMenu); break;
    case ID_LOAD_CONFIRM: {
        std::string name = selectedWorld();
        if (name.empty()) {
            setStatus("No saved worlds");
            break;
        }
        app.genKind = 1;
        app.genName = name;
        app.genState = 1;
        app.genThread = std::thread([name] {
            app.genOk = loadWorld(name, app.world, app.cam);
            app.genState = 2;
        });
        break;
    }
    case ID_LOAD_DELETE: {
        std::string name = selectedWorld();
        if (name.empty()) {
            setStatus("No saved worlds");
            break;
        }
        std::string q = "Delete world \"" + name + "\"? This cannot be undone.";
        if (MessageBoxA(app.hwnd, q.c_str(), "Delete World", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            break;
        std::string path = worldsDir() + "\\" + name + ".ibw";
        setStatus(DeleteFileA(path.c_str()) ? "Deleted " + name : "Could not delete " + name);
        refreshWorldList();
        break;
    }
    case ID_SAVE_WORLD: {
        char buf[128];
        GetWindowTextA(control(ID_SAVE_NAME), buf, sizeof buf);
        std::string name = sanitizeName(buf);
        if (name.empty()) {
            setStatus("Enter a name for the world");
            break;
        }
        app.world.name = name;
        SetWindowTextA(control(ID_SAVE_NAME), name.c_str());
        setStatus(saveWorld(app.world, app.cam) ? "Saved as " + name : "Save failed");
        break;
    }
    case ID_TIME_GO: {
        static const double stepDays[] = {1.0 / 1440.0, 1.0 / 24.0, 1.0, 30.0, 365.0, 3650.0, 36500.0};
        int sel = (int)SendMessageA(control(ID_TIME_STEP), CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < 7) advanceDays(stepDays[sel]);
        SetFocus(app.hwnd);
        break;
    }
    case ID_MAIN_MENU:
        setStatus("");
        setScreen(Screen::MainMenu);
        break;
    }
}

// What is under the cursor, from the CPU mirror of the terrain function.
static const char* SUBSTRATE_NAMES[] = {"soil", "sand", "rock", "scree", "silt", "mud", "ice"};
static const char* COVER_NAMES[] = {"bare", "tundra", "taiga", "forest", "rainforest", "grassland",
                                    "steppe", "savanna", "shrubland", "marsh", "desert"};

// "forest 68%, grassland 22%, rock 10%": cover fractions, with bare ground
// named by its substrate, largest first, down to 5%.
static std::string describeMixture(const terrain::Mixture& m) {
    std::vector<std::pair<float, std::string>> parts;
    for (int i = 1; i < terrain::NCOV; i++) parts.push_back({m.cov[i], COVER_NAMES[i]});
    for (int i = 0; i < terrain::NSUB; i++) parts.push_back({m.cov[0] * m.sub[i], SUBSTRATE_NAMES[i]});
    std::sort(parts.begin(), parts.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::string out;
    for (auto& p : parts) {
        if (p.first < 0.05f || out.size() > 60) break;
        char b[48];
        snprintf(b, sizeof b, "%s%s %d%%", out.empty() ? "" : ", ", p.second.c_str(), (int)std::lround(p.first * 100));
        out += b;
    }
    return out;
}

static std::string describePoint(Vec3 n) {
    const World& wd = app.world;
    terrain::V3 nf = {(float)n.x, (float)n.y, (float)n.z};
    terrain::V3 off = {(float)wd.offset.x, (float)wd.offset.y, (float)wd.offset.z};
    float h = terrain::heightMeters(terrain::rotate(wd.rot, nf) + off, nf, wd.cp, wd.seaLevel, app.octaves,
                                    wd.plateField, wd.rot);
    float lat = (float)std::asin(std::clamp(n.z, -1.0, 1.0));
    float lon = (float)std::atan2(n.y, n.x);
    terrain::V3 wDerive = terrain::rotate(wd.rot, nf) + off;
    // Annual mean drives the mixture (biomes don't change by the hour);
    // the displayed temperature is the current one: seasonal mean plus the
    // diurnal swing phased to local solar time (peak ~14:00).
    atmosphere::DerivedClimate dcTip = atmosphere::deriveAt(wd.clim, lat, lon, wDerive, h);
    float temp = dcTip.temp;
    float tempNow = temp;
    if (!wd.clim.meanT.empty()) {
        float tSeason = sim::seasonalT(wd.clim, nf, std::max(h, 0.0f), wd.simTime);
        float amp = atmosphere::seasonalAt(wd.clim.diurnal, atmosphere::climFuzz(nf), wd.simTime);
        double tod = fmod(wd.simTime, 1.0);
        double hLoc = fmod(lon * (12.0 / PI) + 24.0 * tod + 48.0, 24.0);
        tempNow = tSeason + 0.5f * amp * (float)cos(2 * PI * (hLoc - 14.0) / 24.0);
    }
    char buf[240];
    auto fmtM = [](float m) {
        char b[32];
        snprintf(b, sizeof b, "%d m", (int)std::lround(m));
        return std::string(b);
    };
    // Climatology at the cursor, season-interpolated: shown for sea, lake, and land.
    char climTxt[48] = "";
    if (!wd.clim.rainMmDay.empty()) {
        int ax = (int)(((lon + PI) / (2 * PI)) * atmosphere::W) % atmosphere::W;
        int ay = std::clamp((int)(((lat + PI / 2) / PI) * atmosphere::H), 0, atmosphere::H - 1);
        double sf = fmod(wd.simTime, 365.0) / 365.0 * 4.0 - 0.5;
        int s0 = ((int)std::floor(sf) % 4 + 4) % 4, s1 = (s0 + 1) % 4;
        double f = sf - std::floor(sf);
        int i0 = s0 * atmosphere::W * atmosphere::H + ay * atmosphere::W + ax;
        int i1 = s1 * atmosphere::W * atmosphere::H + ay * atmosphere::W + ax;
        double rain = wd.clim.rainMmDay[i0] * (1 - f) + wd.clim.rainMmDay[i1] * f;
        double snow = wd.clim.snowMmDay[i0] * (1 - f) + wd.clim.snowMmDay[i1] * f;
        if (snow > 0.5 * rain && rain > 0.05)
            snprintf(climTxt, sizeof climTxt, "  |  snow %.1f mm/d", rain);
        else
            snprintf(climTxt, sizeof climTxt, "  |  rain %.1f mm/d", rain);
    }

    if (h < 0) {
        bool frozen = sim::seasonalT(wd.clim, nf, 0.0f, wd.simTime) < sim::FROZEN_T;
        snprintf(buf, sizeof buf, "Sea%s, %s deep  |  %.0f C%s", frozen ? " (frozen)" : "",
                 fmtM(-h).c_str(), tempNow, climTxt);
        return buf;
    }
    // Lake: below the level of any adjacent lake cell (same rule as the shader).
    int cx = (int)std::floor((lon + PI) / (2 * PI) * hydrology::W), cy = (int)std::floor((lat + PI / 2) / PI * hydrology::H);
    cx = hydrology::wrapX(cx);
    cy = std::clamp(cy, 0, hydrology::H - 1);
    // A building under the cursor names itself: same marker positions the
    // shader draws (sim::granaryPos, defined once), pick radius = draw
    // radius plus ~3 px of slop.
    std::string building;
    {
        float pickR = (float)std::clamp(app.cam.kmPerPixel() * 4.0, 1.5, 6.0) +
                      (float)(app.cam.kmPerPixel() * 3.0);
        for (const population::Field::Ruin& r : wd.pop.ruins)
            if (sim::distKm(nf, sim::cellCentre(r.cell)) < pickR) {
                char rb[64];
                snprintf(rb, sizeof rb, "Ruins, abandoned year %d  |  ",
                         (int)(r.abandoned / 365.0) + 1);
                building = rb;
                break;
            }
    }
    if (building.empty() && !wd.pop.settlementAt.empty()) {
        float pickR = (float)std::clamp(app.cam.kmPerPixel() * 2.0, 0.6, 2.5) +
                      (float)(app.cam.kmPerPixel() * 3.0);
        for (int dy = -1; dy <= 1 && building.empty(); dy++)
            for (int dx = -1; dx <= 1 && building.empty(); dx++) {
                int yy = std::clamp(cy + dy, 0, hydrology::H - 1);
                int cell = yy * hydrology::W + hydrology::wrapX(cx + dx);
                int si = wd.pop.settlementAt[cell];
                if (si < 0) continue;
                const population::Settlement& st = wd.pop.settlements[si];
                for (int k = 0; k < (int)(st.granaries + 0.5f) && k < 8; k++)
                    if (sim::distKm(nf, sim::granaryPos(st.cell, k)) < pickR) {
                        building = "Granary  |  ";
                        break;
                    }
            }
    }
    bool nearRiver = false;
    if (!wd.hydro.cells.empty()) {
        const hydrology::Cell& c = wd.hydro.cells[cy * hydrology::W + cx];
        nearRiver = c.nearRiver > 0.5f;
        float lake = hydrology::NO_LAKE;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int yy = std::clamp(cy + dy, 0, hydrology::H - 1);
                lake = std::max(lake, wd.hydro.cells[yy * hydrology::W + hydrology::wrapX(cx + dx)].lakeLevel);
            }
        if (lake > hydrology::NO_LAKE + 1 && h < lake + 12.0f) {
            bool frozen = sim::seasonalT(wd.clim, nf, h, wd.simTime) < sim::FROZEN_T;
            snprintf(buf, sizeof buf, "Lake%s, %s deep  |  %.0f C%s", frozen ? " (frozen)" : "",
                     fmtM(lake + 12.0f - h).c_str(), tempNow, climTxt);
            return buf;
        }
    }
    terrain::V3 w = wDerive;
    float moist = dcTip.moist;
    float slope = terrain::slopeAt(nf, wd.cp, wd.seaLevel, std::min(app.octaves, 12), wd.plateField, wd.rot, off);
    float uplift = wd.plateField.sample({nf.x, nf.y, nf.z}).uplift;
    terrain::Mixture m = terrain::mixtureAt(h, slope, temp, moist, uplift, nearRiver, terrain::patchNoise(w),
                                            dcTip.swamp, dcTip.tCold);
    std::string extra;
    if (!wd.pop.K.empty()) {
        int ci = cy * hydrology::W + cx;
        if (wd.pop.K[ci] > 0) {
            char b[72];
            char gameB[24] = "";
            if (!wd.pop.gameG.empty() && wd.pop.kGameMap[ci] > 0) {
                float g = wd.pop.gameG[population::gameRegion(ci)];
                if (g < 0.98f)
                    snprintf(gameB, sizeof gameB, "  |  game %d%%", (int)std::lround(g * 100));
            }
            snprintf(b, sizeof b, "  |  capacity %d%s",
                     (int)(wd.pop.K[ci] * population::SUSTAIN_R), gameB);
            extra = b;
        }
    }
    snprintf(buf, sizeof buf, "%s%s  |  %.0f C  |  %s%s%s", building.c_str(), fmtM(h).c_str(),
             tempNow, describeMixture(m).c_str(), climTxt, extra.c_str());
    return buf;
}

static void updateTooltip(int x, int y) {
    HWND tip = control(ID_TOOLTIP);
    Vec3 hit;
    if (app.screen != Screen::InGame || !app.cam.hitSphere(x, y, hit)) {
        ShowWindow(tip, SW_HIDE);
        return;
    }
    std::string txt = describePoint(hit);
    SetWindowTextA(tip, txt.c_str());
    int wdt = 12 + (int)txt.size() * 9;
    int tx = std::min(x + 18, app.cam.width - wdt - 4), ty = y + 22;
    if (ty + 26 > app.cam.height) ty = y - 30;
    SetWindowPos(tip, HWND_TOP, tx, ty, wdt, 26, SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

// ------------------------------------------------ selection detail panels
// Custom-painted tabbed windows. Layout constants shared by painting and
// hit-testing; tab hit slots are fixed x-ranges so no text measuring is
// needed to route a click.

constexpr int PANEL_W = 380, PANEL_H = 312, PANEL_BAND_H = 260;
constexpr int PANEL_PAD = 12, PANEL_TAB_Y = 36, PANEL_TAB_H = 24, PANEL_CONTENT_Y = 70,
              PANEL_LINE_H = 21;
static const int PANEL_TAB_X[4] = {12, 126, 236, 340}; // slot edges for 3 tabs
static const char* PANEL_TABS[3] = {"Environment", "Technology", "Buildings"};
static const char* TECH_NAMES[population::NTECH] = {"Farming", "Husbandry", "Granary building"};

// Settlements are erased when they pick up and leave, so panels hold an id
// and resolve the index whenever they draw.
static int settlementIndexById(uint32_t sid) {
    const std::vector<population::Settlement>& v = app.world.pop.settlements;
    for (int i = 0; i < (int)v.size(); i++)
        if (v[i].id == sid) return i;
    return -1;
}

static std::string fmtYears(double yr) {
    char b[32];
    if (yr >= 100000) return "100k+ yr";
    if (yr >= 1000) snprintf(b, sizeof b, "%.1fk yr", yr / 1000.0);
    else snprintf(b, sizeof b, "%.0f yr", yr);
    return b;
}

static std::string techStateLine(const population::TechState& ts, int techId, double now) {
    if (!ts.practising)
        return std::string(TECH_NAMES[techId]) + (ts.aware ? ": known, not practised" : ": unknown");
    char b[64];
    snprintf(b, sizeof b, "%s: practising, expertise %d%%", TECH_NAMES[techId],
             (int)std::lround(technology::expertise(ts, now) * 100));
    return b;
}

static std::string envText(const population::Settlement& st) {
    const World& wd = app.world;
    double now = wd.simTime;
    terrain::V3 n = sim::cellCentre(st.cell);
    float lat = std::asin(std::clamp(n.z, -1.0f, 1.0f)) * 180.0f / 3.14159265f;
    float lon = std::atan2(n.y, n.x) * 180.0f / 3.14159265f;
    float awareKm = population::settlementAwareKm(now - st.founded,
                                                  sim::prominenceM(wd.hydro, wd.clim, st.cell));
    char b[128];
    std::string out;
    snprintf(b, sizeof b, "%.1f%c  %.1f%c\n", std::fabs(lat), lat >= 0 ? 'N' : 'S',
             std::fabs(lon), lon >= 0 ? 'E' : 'W');
    out += b;
    snprintf(b, sizeof b, "People: %d (capacity %d)\n", (int)st.P,
             (int)(technology::effectiveK(st, now) * population::SUSTAIN_R));
    out += b;
    snprintf(b, sizeof b, "Stores: %d of %d days\n", (int)(st.S / std::max(st.P, 1.0f)),
             (int)population::storageCapDays(st.P, st.granaries));
    out += b;
    snprintf(b, sizeof b, "Land condition: %d%%\n", (int)std::lround(st.R * 100));
    out += b;
    if (st.kGame > 0) {
        float plantF = st.kFoodP - st.kGame;
        float gameF = st.kGame * population::huntEff(st.gameNow);
        int dietG = (int)std::lround(gameF / std::max(plantF + gameF, 1e-6f) * 100);
        snprintf(b, sizeof b, "Wild game: %d%%%s (diet %d%% game)\n",
                 (int)std::lround(st.gameNow * 100),
                 st.gameNow < population::GAME_FLOOR ? " -- gone" : "", dietG);
        out += b;
    }
    snprintf(b, sizeof b, "Farm suitability: %d%%   pasture: %d%%\n",
             (int)std::lround(st.sFarm * 100), (int)std::lround(st.pasture * 100));
    out += b;
    snprintf(b, sizeof b, "Build materials: %d%%\n", (int)std::lround(st.buildMat * 100));
    out += b;
    snprintf(b, sizeof b, "Awareness: %d km\n", (int)awareKm);
    out += b;
    if (st.herd > 0.5f) {
        snprintf(b, sizeof b, "Livestock: feeds %d\n", (int)st.herd);
        out += b;
    }
    if (st.starvedYr >= 0.5f) {
        snprintf(b, sizeof b, "Hunger: %d lost this year\n", (int)std::lround(st.starvedYr));
        out += b;
    }
    if (st.scarceSince >= 0) {
        snprintf(b, sizeof b, "Scarce since year %d%s\n", (int)(st.scarceSince / 365.0) + 1,
                 st.noProspect ? " -- nowhere to go" : "");
        out += b;
    }
    return out;
}

// The exact numbers behind one technology's chances here: invention weight,
// contact odds, and every adoption gate, with the blocked one named.
static std::string techDetailText(const population::Settlement& st, int idx, int t) {
    const World& wd = app.world;
    double now = wd.simTime;
    char b[160];
    std::string out = "< back\n";
    const population::TechState& ts = st.tech[t];
    out += TECH_NAMES[t];
    out += ts.practising ? " -- practising" : ts.aware ? " -- known, not practised" : " -- unknown";
    out += "\n";
    if (ts.practising) {
        snprintf(b, sizeof b, "Expertise %d%% since year %d\n(matures toward 100%% over ~50 yr)\n",
                 (int)std::lround(technology::expertise(ts, now) * 100),
                 (int)(ts.practiceT / 365.0) + 1);
        out += b;
        return out;
    }
    if (!ts.aware) {
        if (technology::needDriven(t)) {
            float years =
                t == population::TECH_FARMING
                    ? (st.hungrySince >= 0 ? (float)((now - st.hungrySince) / 365.0) : 0.0f)
                    : st.granNeedYrs;
            float acute = std::clamp((years - technology::NEED_YEARS_ON) /
                                         (technology::NEED_YEARS_SAT - technology::NEED_YEARS_ON),
                                     0.0f, 1.0f);
            float w = technology::needWeight(st, t, now);
            double W = 0;
            for (const population::Settlement& o : wd.pop.settlements)
                W += technology::needWeight(o, t, now);
            out += "Invention, driven by need:\n";
            snprintf(b, sizeof b, " %s %.1f yr -> desperation %d%%\n",
                     t == population::TECH_FARMING ? "hungry" : "stores binding", years,
                     (int)std::lround(acute * 100));
            out += b;
            snprintf(b, sizeof b, " suitability %d%%, people x%.2f\n",
                     (int)std::lround(technology::suitability(st, t) * 100),
                     std::min(st.P / 300.0f, 3.0f));
            out += b;
            if (W > 0) {
                snprintf(b, sizeof b, " weight %.2f of world %.1f (share %d%%)\n", w, W,
                         (int)std::lround(w / W * 100));
                out += b;
                snprintf(b, sizeof b, " world mean %s\n",
                         fmtYears(technology::NEED_MEAN_YEARS / std::sqrt(W)).c_str());
                out += b;
            } else
                out += " nobody in the world needs it yet\n";
        } else {
            double unaware = 0, total = 0;
            for (const population::Settlement& o : wd.pop.settlements) {
                total += o.P;
                if (!o.tech[t].aware) unaware += o.P;
            }
            double share = total > 0 ? unaware / total : 0;
            out += "Invention, serendipity:\n";
            snprintf(b, sizeof b, " unaware share %d%% -> world mean %s\n",
                     (int)std::lround(share * 100),
                     share > 0 ? fmtYears(technology::INVENT_MEAN_YEARS / share).c_str() : "never");
            out += b;
        }
        int knowing = 0;
        for (int j : wd.pop.neighbours[idx]) knowing += wd.pop.settlements[j].tech[t].aware ? 1 : 0;
        if (knowing)
            snprintf(b, sizeof b, "Hearing of it: %d neighbour%s -> mean %.1f yr\n", knowing,
                     knowing == 1 ? " knows it" : "s know it",
                     technology::AWARE_MEAN_YEARS / knowing);
        else
            snprintf(b, sizeof b, "Hearing of it: nobody within %d km knows\n",
                     (int)population::CONTACT_KM);
        out += b;
        return out;
    }
    float suit = technology::suitability(st, t);
    float need = technology::adoptionNeed(st, t, now);
    float esum = 0;
    int teachers = 0;
    for (int j : wd.pop.neighbours[idx]) {
        float e = technology::expertise(wd.pop.settlements[j].tech[t], now);
        if (e > 0) { esum += e; teachers++; }
    }
    float phi = st.P > 1 ? technology::effectiveK(st, now) * st.R / st.P : 2.0f;
    out += "Adoption gates (all must be open):\n";
    snprintf(b, sizeof b, " suitable ground: %d%%%s\n", (int)std::lround(suit * 100),
             suit <= 0 ? "  <- BLOCKED" : "");
    out += b;
    if (t == population::TECH_GRANARY)
        snprintf(b, sizeof b, " need: %d%% (stores bound %.0f yr running)%s\n",
                 (int)std::lround(need * 100), st.granNeedYrs, need <= 0 ? "  <- BLOCKED" : "");
    else
        snprintf(b, sizeof b, " need: %d%% (phi %.2f, content at 1.11)%s\n",
                 (int)std::lround(need * 100), phi, need <= 0 ? "  <- BLOCKED" : "");
    out += b;
    snprintf(b, sizeof b, " teachers in %d km: %d, expertise sum %.2f%s\n",
             (int)population::CONTACT_KM, teachers, esum, esum <= 0 ? "  <- BLOCKED" : "");
    out += b;
    double rate = (double)suit * need * esum;
    if (rate > 0)
        snprintf(b, sizeof b, "-> mean wait %s\n",
                 fmtYears(technology::PRACT_MEAN_YEARS / rate).c_str());
    else
        snprintf(b, sizeof b, "-> will not adopt until unblocked\n");
    out += b;
    return out;
}

static std::string buildingsText(const population::Settlement& st, double now) {
    char b[96];
    std::string out;
    if (st.granaries > 0.5f) snprintf(b, sizeof b, "Granaries: %d\n", (int)st.granaries);
    else snprintf(b, sizeof b, "Granaries: none\n");
    out += b;
    if (st.buildWork > 0) {
        snprintf(b, sizeof b, "Under construction: %d%% done\n",
                 (int)std::lround((1.0 - st.buildWork / population::GRANARY_WORK) * 100));
        out += b;
    }
    snprintf(b, sizeof b, "Storage: %d + %d days per person\n",
             (int)population::CAP_DAYS_SETTLED,
             (int)(population::storageCapDays(st.P, st.granaries) - population::CAP_DAYS_SETTLED));
    out += b;
    const population::TechState& gt = st.tech[population::TECH_GRANARY];
    if (gt.practising) {
        snprintf(b, sizeof b, "Build pace: craft %d%% x materials %d%%\n",
                 (int)std::lround(technology::expertise(gt, now) * 100),
                 (int)std::lround(st.buildMat * 100));
        out += b;
        if (st.buildWork <= 0) out += "No build under way: stores have\nnot both filled and drained.\n";
    } else if (gt.aware)
        out += "(knows the craft is possible,\nhas not learned it)\n";
    else
        out += "(granary building unknown here)\n";
    return out;
}

static std::string bandText(uint32_t bandId) {
    const World& wd = app.world;
    double now = wd.simTime;
    for (const population::Band& bd : wd.pop.bands)
        if (bd.id == bandId) {
            char b[128];
            std::string out;
            float away = sim::distKm({bd.px, bd.py, bd.pz}, sim::cellCentre(bd.targetCell));
            double rest = bd.resting ? now - bd.restStart : 0.0;
            float awareKm = population::bandAwareKm(
                rest, sim::prominenceM(wd.hydro, wd.clim, sim::cellOf({bd.px, bd.py, bd.pz})));
            snprintf(b, sizeof b, "People: %d\nStores: %d days\nState: %s\nTarget: %d km away\n",
                     (int)bd.P, (int)(bd.S / std::max(bd.P, 1.0f)),
                     bd.resting ? "resting" : "moving", (int)away);
            out += b;
            for (int t = 0; t < population::NTECH; t++)
                out += techStateLine(bd.tech[t], t, now) + "\n";
            snprintf(b, sizeof b, "Awareness: %d km\n", (int)awareKm);
            out += b;
            return out;
        }
    return "No longer on the move:\nsettled, merged, or perished.";
}

static std::string panelContent(const Panel& pn) {
    if (pn.kind == 1) return bandText(pn.bandId);
    int idx = settlementIndexById(pn.sid);
    if (idx < 0) return "This settlement is gone:\nthey picked up and moved on.";
    const population::Settlement& st = app.world.pop.settlements[idx];
    if (pn.tab == 0) return envText(st);
    if (pn.tab == 2) return buildingsText(st, app.world.simTime);
    if (pn.techSel >= 0) return techDetailText(st, idx, pn.techSel);
    std::string out;
    for (int t = 0; t < population::NTECH; t++)
        out += techStateLine(st.tech[t], t, app.world.simTime) + "\n";
    out += "\n(click a technology for details)";
    return out;
}

static Panel* panelFor(HWND h) {
    for (Panel& p : app.panels)
        if (p.wnd == h) return &p;
    return nullptr;
}

static void paintPanel(HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    FillRect(dc, &rc, app.bgBrush);
    SetBkMode(dc, TRANSPARENT);
    Panel* pn = panelFor(h);
    if (pn) {
        char title[48];
        if (pn->kind == 0) snprintf(title, sizeof title, "Settlement %u", pn->sid);
        else snprintf(title, sizeof title, "Band %u", pn->bandId);
        SelectObject(dc, app.panelBold);
        SetTextColor(dc, RGB(235, 235, 240));
        TextOutA(dc, PANEL_PAD, 8, title, (int)strlen(title));
        SelectObject(dc, app.panelFont);
        if (pn->kind == 0)
            for (int t = 0; t < 3; t++) {
                SetTextColor(dc, pn->tab == t ? RGB(255, 225, 150) : RGB(140, 140, 155));
                TextOutA(dc, PANEL_TAB_X[t], PANEL_TAB_Y, PANEL_TABS[t],
                         (int)strlen(PANEL_TABS[t]));
            }
        std::string txt = panelContent(*pn);
        int y = pn->kind == 0 ? PANEL_CONTENT_Y : PANEL_TAB_Y;
        int row = 0;
        size_t pos = 0;
        while (pos <= txt.size()) {
            size_t e = txt.find('\n', pos);
            std::string line =
                txt.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
            bool clickable = pn->kind == 0 && pn->tab == 1 &&
                             ((pn->techSel < 0 && row < population::NTECH) ||
                              (pn->techSel >= 0 && row == 0));
            SetTextColor(dc, clickable ? RGB(255, 215, 130) : RGB(230, 230, 235));
            TextOutA(dc, PANEL_PAD, y, line.c_str(), (int)line.size());
            y += PANEL_LINE_H;
            row++;
            if (e == std::string::npos) break;
            pos = e + 1;
        }
    }
    EndPaint(h, &ps);
}

static void refreshPanels() {
    for (const Panel& pn : app.panels) InvalidateRect(pn.wnd, nullptr, TRUE);
}

static void closeAllPanels() {
    std::vector<Panel> panels = app.panels; // DestroyWindow mutates app.panels
    for (const Panel& pn : panels)
        if (IsWindow(pn.wnd)) DestroyWindow(pn.wnd);
    app.panels.clear();
    app.panelSpawn = 0;
}

static LRESULT CALLBACK panelProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:
        paintPanel(h);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == 1) DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        if (app.panelDrag == h) { ReleaseCapture(); app.panelDrag = nullptr; }
        for (size_t i = 0; i < app.panels.size(); i++)
            if (app.panels[i].wnd == h) { app.panels.erase(app.panels.begin() + i); break; }
        return 0;
    case WM_LBUTTONDOWN: {
        Panel* pn = panelFor(h);
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); // raise on grab
        if (pn && pn->kind == 0 && my >= PANEL_TAB_Y && my < PANEL_TAB_Y + PANEL_TAB_H) {
            for (int t = 0; t < 3; t++)
                if (mx >= PANEL_TAB_X[t] && mx < PANEL_TAB_X[t + 1] - 6) {
                    pn->tab = t;
                    pn->techSel = -1;
                    InvalidateRect(h, nullptr, TRUE);
                    return 0;
                }
        }
        if (pn && pn->kind == 0 && pn->tab == 1 && my >= PANEL_CONTENT_Y) {
            int row = (my - PANEL_CONTENT_Y) / PANEL_LINE_H;
            if (pn->techSel < 0 && row >= 0 && row < population::NTECH) {
                pn->techSel = row;
                InvalidateRect(h, nullptr, TRUE);
                return 0;
            }
            if (pn->techSel >= 0 && row == 0) { // "< back"
                pn->techSel = -1;
                InvalidateRect(h, nullptr, TRUE);
                return 0;
            }
        }
        // Anywhere else grabs the window for dragging.
        SetCapture(h);
        app.panelDrag = h;
        app.panelDragOff = {mx, my};
        return 0;
    }
    case WM_MOUSEMOVE:
        if (app.panelDrag == h) {
            POINT c;
            GetCursorPos(&c);
            ScreenToClient(app.hwnd, &c);
            SetWindowPos(h, nullptr, c.x - app.panelDragOff.x, c.y - app.panelDragOff.y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER);
        }
        return 0;
    case WM_LBUTTONUP:
        if (app.panelDrag == h) {
            ReleaseCapture();
            app.panelDrag = nullptr;
        }
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static void openPanel(int kind, uint32_t sid, uint32_t bandId) {
    for (const Panel& pn : app.panels)
        if (pn.kind == kind && pn.sid == sid && pn.bandId == bandId) {
            SetWindowPos(pn.wnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            return; // already open: raise it instead of stacking a twin
        }
    int pw = PANEL_W, ph = kind == 0 ? PANEL_H : PANEL_BAND_H;
    int x = 16 + (app.panelSpawn % 7) * 30, y = 56 + (app.panelSpawn % 7) * 30;
    app.panelSpawn++;
    HINSTANCE inst = GetModuleHandleA(nullptr);
    HWND w = CreateWindowA("IBPanel", "", WS_CHILD | WS_BORDER | WS_VISIBLE | WS_CLIPSIBLINGS,
                           x, y, pw, ph, app.hwnd, nullptr, inst, nullptr);
    HWND btn = CreateWindowA("BUTTON", "X", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, pw - 34, 6, 24, 24,
                             w, (HMENU)1, inst, nullptr);
    SendMessageA(btn, WM_SETFONT, (WPARAM)app.font, TRUE);
    app.panels.push_back({w, kind, sid, bandId});
    SetWindowPos(w, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

// A click on the globe: open a detail panel for the settlement or band whose
// marker is under the cursor (same radii the shader draws them with).
static void pickAt(int x, int y) {
    Vec3 hit;
    if (app.screen != Screen::InGame || !app.cam.hitSphere(x, y, hit)) return;
    terrain::V3 n = {(float)hit.x, (float)hit.y, (float)hit.z};
    const population::Field& pf = app.world.pop;
    // Pick radius = draw radius plus ~3 px of slop: clicks are aim-limited.
    float slop = (float)(app.cam.kmPerPixel() * 3.0);
    float sRadius = (float)std::clamp(app.cam.kmPerPixel() * 5.0, 4.0, 18.0) + slop;
    int best = -1;
    float bestD = sRadius;
    for (int i = 0; i < (int)pf.settlements.size(); i++) {
        float d = sim::distKm(n, sim::cellCentre(pf.settlements[i].cell));
        if (d < bestD) { bestD = d; best = i; }
    }
    if (best >= 0) { openPanel(0, pf.settlements[best].id, 0); return; }
    float bRadius = (float)std::clamp(app.cam.kmPerPixel() * 4.0, 3.0, 14.0) + slop;
    uint32_t bestId = 0;
    bestD = bRadius;
    for (const population::Band& bd : pf.bands) {
        // Measure to the cell centre: that is where the shader draws the dot.
        float d = sim::distKm(n, sim::cellCentre(sim::cellOf({bd.px, bd.py, bd.pz})));
        if (d < bestD) { bestD = d; bestId = bd.id; }
    }
    if (bestId) openPanel(1, 0, bestId);
}

// Step the paused clock forward and let every settlement catch up. Wakes
// repeat until nothing is due, so a year's jump replays each settlement's
// scheduled re-evaluations in order.
// Calendar: 365-day years (no leap days), Gregorian month lengths,
// time 0 = 0001-01-01 00:00.
static std::string simDate() {
    static const int ML[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    long long mins = (long long)std::llround(app.world.simTime * 1440.0);
    int minute = (int)(mins % 60), hour = (int)(mins / 60 % 24);
    int total = (int)(mins / 1440);
    int year = total / 365 + 1, doy = total % 365, month = 0;
    while (doy >= ML[month]) { doy -= ML[month]; month++; }
    char b[40];
    snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d", year, month + 1, doy + 1, hour, minute);
    return b;
}

static void updateDateLabel() {
    SetWindowTextA(control(ID_DATE_LABEL), simDate().c_str());
}

static void advanceDays(double days) {
    app.world.simTime += days;
    updateDateLabel();
    population::Field& pf = app.world.pop;
    if (pf.settlements.empty()) return;
    bool any = sim::simulate(pf, app.world.tech, app.world.hydro, app.world.clim, app.world.simTime);
    if (any && app.popTex) uploadPopulation();
    refreshPanels();
}

static void applyDrag(int x, int y) {
    Camera& c = app.cam;
    Vec3 hit;
    if (app.anchorValid && c.hitSphere(x, y, hit)) {
        // Rotate the camera so the grabbed surface point follows the cursor.
        double latHit = std::asin(std::clamp(hit.z, -1.0, 1.0));
        double lonHit = std::atan2(hit.y, hit.x);
        double latAnc = std::asin(std::clamp(app.anchor.z, -1.0, 1.0));
        double lonAnc = std::atan2(app.anchor.y, app.anchor.x);
        double dLon = lonHit - lonAnc;
        if (dLon > PI) dLon -= 2 * PI;
        if (dLon < -PI) dLon += 2 * PI;
        c.lon -= dLon;
        c.lat -= latHit - latAnc;
    } else {
        // Cursor is off the globe: rotate by a pixel-proportional amount.
        double radPerPx = std::min(2.0 * c.altitude * c.tanHalfV() / c.height, PI / c.height);
        c.lon -= (x - app.lastX) * radPerPx;
        c.lat += (y - app.lastY) * radPerPx;
    }
    c.lat = std::clamp(c.lat, -89.0 * PI / 180, 89.0 * PI / 180);
    while (c.lon > PI) c.lon -= 2 * PI;
    while (c.lon < -PI) c.lon += 2 * PI;
}

static void onEscape() {
    switch (app.screen) {
    case Screen::InGame: setScreen(Screen::PauseMenu); break;
    case Screen::PauseMenu: setScreen(Screen::InGame); break;
    case Screen::LoadMenu:
    case Screen::NewWorldMenu: setScreen(Screen::MainMenu); break;
    case Screen::MainMenu: break;
    }
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        app.cam.width = std::max(1, (int)LOWORD(lp));
        app.cam.height = std::max(1, (int)HIWORD(lp));
        app.cam.clampAltitude();
        glViewport(0, 0, app.cam.width, app.cam.height);
        if (!app.controls.empty()) layoutControls();
        return 0;
    case WM_COMMAND:
        if (HIWORD(wp) == BN_CLICKED) onCommand(LOWORD(wp));
        else if (HIWORD(wp) == LBN_DBLCLK) onCommand(ID_LOAD_CONFIRM);
        return 0;
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wp, RGB(230, 230, 235));
        SetBkColor((HDC)wp, RGB(8, 8, 16));
        return (LRESULT)app.bgBrush;
    case WM_LBUTTONDOWN:
        if (app.screen != Screen::InGame) return 0;
        SetCapture(hwnd);
        SetFocus(hwnd);
        app.dragging = true;
        app.lastX = GET_X_LPARAM(lp);
        app.lastY = GET_Y_LPARAM(lp);
        app.downX = app.lastX;
        app.downY = app.lastY;
        app.clickMoved = false;
        app.anchorValid = app.cam.hitSphere(app.lastX, app.lastY, app.anchor);
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        if (app.dragging && !app.clickMoved) pickAt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        app.dragging = false;
        return 0;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (app.dragging) {
            if (std::abs(x - app.downX) + std::abs(y - app.downY) > 4) app.clickMoved = true;
            applyDrag(x, y);
            app.lastX = x;
            app.lastY = y;
        }
        updateTooltip(x, y);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (app.screen != Screen::InGame) return 0;
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        double factor = std::pow(0.8, delta / (double)WHEEL_DELTA);
        app.cam.altitude *= factor;
        app.cam.clampAltitude();
        return 0;
    }
    case WM_KEYDOWN:
        if (app.genState != 0) return 0;
        if (wp == VK_F2) { app.shotPath = "dbg_shot.bmp"; return 0; } // back-buffer screenshot
        if (app.screen == Screen::InGame) {
            int mode = wp == 'P' ? 1 : wp == 'B' ? 2 : wp == 'V' ? 3 : wp == 'K' ? 4 : 0;
            if (mode) app.debugMode = app.debugMode == mode ? 0 : mode;
        }
        return 0;
    case WM_CLOSE:
        app.running = false;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- main

int main(int argc, char** argv) {
    HINSTANCE inst = GetModuleHandleA(nullptr);
    WNDCLASSA wc = {};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    wc.lpszClassName = "HumanHistory";
    RegisterClassA(&wc);

    WNDCLASSA pc = {};
    pc.lpfnWndProc = panelProc;
    pc.hInstance = inst;
    pc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    pc.hbrBackground = CreateSolidBrush(RGB(8, 8, 16));
    pc.lpszClassName = "IBPanel";
    RegisterClassA(&pc);

    RECT r = {0, 0, app.cam.width, app.cam.height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    app.hwnd = CreateWindowA("HumanHistory", "Human History",
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                             nullptr, nullptr, inst, nullptr);
    HWND hwnd = app.hwnd;
    HDC dc = GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
    HGLRC rc = wglCreateContext(dc);
    wglMakeCurrent(dc, rc);
    if (!loadGL()) return 1;
    if (wglSwapIntervalEXT) wglSwapIntervalEXT(1);

    app.program = buildProgram();
    if (!app.program) return 1;
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glUseProgram(app.program);

    GLint uCamPos = glGetUniformLocation(app.program, "uCamPos");
    GLint uSun = glGetUniformLocation(app.program, "uSun");
    GLint uForward = glGetUniformLocation(app.program, "uForward");
    GLint uRight = glGetUniformLocation(app.program, "uRight");
    GLint uUp = glGetUniformLocation(app.program, "uUp");
    GLint uTanHalf = glGetUniformLocation(app.program, "uTanHalf");
    GLint uAspect = glGetUniformLocation(app.program, "uAspect");
    GLint uOctaves = glGetUniformLocation(app.program, "uOctaves");
    GLint uKmPerPixel = glGetUniformLocation(app.program, "uKmPerPixel");
    GLint uWorldRot = glGetUniformLocation(app.program, "uWorldRot");
    GLint uWorldOff = glGetUniformLocation(app.program, "uWorldOff");
    GLint uDim = glGetUniformLocation(app.program, "uDim");
    GLint uFreq = glGetUniformLocation(app.program, "uFreq");
    GLint uWarp = glGetUniformLocation(app.program, "uWarp");
    GLint uWebness = glGetUniformLocation(app.program, "uWebness");
    GLint uSeaLevel = glGetUniformLocation(app.program, "uSeaLevel");
    GLint uHydro = glGetUniformLocation(app.program, "uHydro");
    GLint uHasHydro = glGetUniformLocation(app.program, "uHasHydro");
    GLint uScaleBar = glGetUniformLocation(app.program, "uScaleBar");
    GLint uDebugMode = glGetUniformLocation(app.program, "uDebugMode");
    std::string lastScaleText;
    glUniform1i(uHydro, 0);
    glUniform1i(glGetUniformLocation(app.program, "uPlates"), 1);
    glUniform1i(glGetUniformLocation(app.program, "uPop"), 2);
    glUniform1i(glGetUniformLocation(app.program, "uClim"), 3);
    glUniform1i(glGetUniformLocation(app.program, "uClim2"), 4);
    GLint uDoy = glGetUniformLocation(app.program, "uDoy");
    GLint uClock = glGetUniformLocation(app.program, "uClock");
    GLint uAware = glGetUniformLocation(app.program, "uAware");
    GLint uAwareCount = glGetUniformLocation(app.program, "uAwareCount");

    createControls();
    app.cam.clampAltitude();

    // Testing shortcut:
    // humanhistory <latDeg> <lonDeg> [altitudeKm] [seed] [land%] [conc%] [debugmode] [fastForwardYears]
    // A seed of "@name" loads worlds\name.ibw instead of generating (testing).
    if (argc >= 3) {
        bool loaded = argc >= 5 && argv[4][0] == '@' &&
                      loadWorld(argv[4] + 1, app.world, app.cam);
        if (!loaded) {
            app.world.seed = argc >= 5 ? (uint32_t)strtoul(argv[4], nullptr, 10) : 0;
            if (argc >= 6) app.world.landPercent = (float)atof(argv[5]);
            if (argc >= 7) app.world.concentration = (float)atof(argv[6]);
            app.world.build();
        }
        if (argc >= 8) {
            std::string d = argv[7];
            app.debugMode = d == "plates" ? 1 : d == "substrate" ? 2 : d == "vegetation" ? 3
                          : d == "population" ? 4 : d == "climate" ? 5 : 0;
        }
        uploadHydrology();
        app.cam.lat = atof(argv[1]) * PI / 180;
        app.cam.lon = atof(argv[2]) * PI / 180;
        if (argc >= 4) app.cam.altitude = atof(argv[3]) / EARTH_RADIUS_KM;
        app.cam.clampAltitude();
        setScreen(Screen::InGame);
        if (argc >= 9) {
            advanceDays(atof(argv[8]) * 365.0); // fast-forward years
            int gran = 0, building = 0;
            for (const population::Settlement& s : app.world.pop.settlements) {
                gran += (int)(s.granaries + 0.5f);
                building += s.buildWork > 0 ? 1 : 0;
            }
            double totalP = 0;
            for (const population::Settlement& s : app.world.pop.settlements) totalP += s.P;
            fprintf(stderr,
                    "granaries built: %d, under construction: %d\n"
                    "settlements: %d, people: %.0f, bands: %d, ruins: %d, worked sites: %d\n",
                    gran, building, (int)app.world.pop.settlements.size(), totalP,
                    (int)app.world.pop.bands.size(), (int)app.world.pop.ruins.size(),
                    (int)app.world.pop.scars.size());
        }
        if (argc >= 10) app.shotPath = argv[9];            // save a frame, then keep running
    } else {
        setScreen(Screen::MainMenu);
    }

    LARGE_INTEGER qpf, fpsT0;
    QueryPerformanceFrequency(&qpf);
    QueryPerformanceCounter(&fpsT0);
    int fpsFrames = 0;
    while (app.running) {
        if (app.genState == 2) finishGeneration();
        MSG m;
        while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
            // Buttons take keyboard focus, so catch Escape before it reaches them.
            if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) {
                onEscape();
                continue;
            }
            if (m.message == WM_KEYDOWN && m.wParam == VK_RETURN) {
                if (app.screen == Screen::PauseMenu) { onCommand(ID_SAVE_WORLD); continue; }
                if (app.screen == Screen::NewWorldMenu) { onCommand(ID_GEN_CREATE); continue; }
            }
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
        bool showGlobe = app.screen == Screen::InGame || app.screen == Screen::PauseMenu;
        if (showGlobe) {
            Camera& c = app.cam;
            Vec3 p = c.position(), f = c.forward(), rt = c.right(), u = c.up();
            // Finest noise octave should be around two pixels wide on screen.
            double kmpp = c.kmPerPixel();
            int octaves = (int)std::ceil(std::log2(EARTH_RADIUS_KM / (2.0 * kmpp)));
            octaves = std::clamp(octaves, 4, 16);
            app.octaves = octaves;

            glUniform3f(uCamPos, (float)p.x, (float)p.y, (float)p.z);
            glUniform1f(uDoy, (float)fmod(app.world.simTime, 365.0));
            glUniform1f(uClock, (float)fmod(app.world.simTime, 4096.0));
            {
                // Awareness zones for entities with open detail panels.
                float aw[8 * 4] = {};
                int nAw = 0;
                for (const Panel& pn : app.panels) {
                    if (nAw >= 8) break;
                    terrain::V3 e{};
                    float radius = 0;
                    int sIdx = pn.kind == 0 ? settlementIndexById(pn.sid) : -1;
                    if (pn.kind == 0 && sIdx < 0) continue; // moved on: no zone to draw
                    if (pn.kind == 0) {
                        const population::Settlement& st = app.world.pop.settlements[sIdx];
                        e = sim::cellCentre(st.cell);
                        radius = population::settlementAwareKm(
                            app.world.simTime - st.founded,
                            sim::prominenceM(app.world.hydro, app.world.clim, st.cell));
                    } else {
                        for (const population::Band& bd : app.world.pop.bands)
                            if (bd.id == pn.bandId) {
                                e = {bd.px, bd.py, bd.pz};
                                double rest = bd.resting ? app.world.simTime - bd.restStart : 0.0;
                                radius = population::bandAwareKm(
                                    rest, sim::prominenceM(app.world.hydro, app.world.clim,
                                                           sim::cellOf(e)));
                                break;
                            }
                    }
                    if (radius <= 0) continue;
                    aw[nAw * 4 + 0] = e.x;
                    aw[nAw * 4 + 1] = e.y;
                    aw[nAw * 4 + 2] = e.z;
                    aw[nAw * 4 + 3] = radius;
                    nAw++;
                }
                glUniform4fv(uAware, 8, aw);
                glUniform1i(uAwareCount, nAw);
            }
            {
                // Subsolar point from the sim clock: one lap per day westward
                // (solar noon at longitude 0 at 12:00), declination +-23.5 deg
                // peaking at the June 21 solstice (day 171 of the 365-day year).
                // Computed in daylight.h -- the same formulas that set work
                // hours and band travel, so the lit hemisphere on screen and
                // the sim's activity can never drift apart.
                double dec, hour;
                daylight::subsolar(app.world.simTime, dec, hour);
                glUniform3f(uSun, (float)(cos(dec) * cos(hour)), (float)(cos(dec) * sin(hour)),
                            (float)sin(dec));
            }
            glUniform3f(uForward, (float)f.x, (float)f.y, (float)f.z);
            glUniform3f(uRight, (float)rt.x, (float)rt.y, (float)rt.z);
            glUniform3f(uUp, (float)u.x, (float)u.y, (float)u.z);
            glUniform1f(uTanHalf, (float)c.tanHalfV());
            glUniform1f(uAspect, (float)c.aspect());
            glUniform1i(uOctaves, octaves);
            glUniform1f(uKmPerPixel, (float)kmpp);
            glUniformMatrix3fv(uWorldRot, 1, GL_FALSE, app.world.rot);
            glUniform3f(uWorldOff, (float)app.world.offset.x, (float)app.world.offset.y,
                        (float)app.world.offset.z);
            glUniform1f(uDim, app.screen == Screen::PauseMenu ? 0.35f : 1.0f);
            glUniform1f(uFreq, app.world.cp.freq);
            glUniform1f(uWarp, app.world.cp.warp);
            glUniform1f(uWebness, app.world.cp.webness);
            glUniform1f(uSeaLevel, app.world.seaLevel);
            glUniform1i(uHasHydro, app.world.hydro.cells.empty() ? 0 : 1);
            glUniform1i(uDebugMode, app.debugMode);
            if (app.screen == Screen::InGame) {
                ScaleBar sb = chooseScale(kmpp);
                // Pixel coordinates with origin bottom-left, as gl_FragCoord uses.
                float x0 = (float)SCALE_MARGIN, y0 = (float)SCALE_MARGIN + 6;
                glUniform4f(uScaleBar, x0, y0, x0 + sb.px, y0);
                std::string txt = scaleText(sb.km);
                if (txt != lastScaleText) {
                    lastScaleText = txt;
                    SetWindowTextA(control(ID_SCALE_LABEL), txt.c_str());
                }
            } else {
                glUniform4f(uScaleBar, -1, -1, -1, -1);
            }
            glBindTexture(GL_TEXTURE_2D, app.hydroTex);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            // Self-screenshot from the back buffer: defined even when the
            // window is occluded, unlike PrintWindow. Testing tooling.
            if (!app.shotPath.empty()) {
                int W = app.cam.width, H = app.cam.height;
                std::vector<unsigned char> px(W * H * 3);
                glReadPixels(0, 0, W, H, 0x80E0 /*GL_BGR*/, GL_UNSIGNED_BYTE, px.data());
                int rowPad = (4 - (W * 3) % 4) % 4, stride = W * 3 + rowPad;
                unsigned int imgSize = stride * H, fileSize = 54 + imgSize;
                unsigned char hdr[54] = {'B', 'M'};
                *(unsigned int*)(hdr + 2) = fileSize;
                *(unsigned int*)(hdr + 10) = 54;
                *(unsigned int*)(hdr + 14) = 40;
                *(int*)(hdr + 18) = W;
                *(int*)(hdr + 22) = H; // bottom-up, matching glReadPixels
                *(unsigned short*)(hdr + 26) = 1;
                *(unsigned short*)(hdr + 28) = 24;
                *(unsigned int*)(hdr + 34) = imgSize;
                std::ofstream f(app.shotPath, std::ios::binary);
                f.write((char*)hdr, 54);
                unsigned char pad[4] = {};
                for (int yy = 0; yy < H; yy++) {
                    f.write((char*)&px[yy * W * 3], W * 3);
                    f.write((char*)pad, rowPad);
                }
                fprintf(stderr, "shot: %s\n", app.shotPath.c_str());
                app.shotPath.clear();
            }
        } else {
            glClearColor(8 / 255.f, 8 / 255.f, 16 / 255.f, 1);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        SwapBuffers(dc);

        // Frame rate in the title bar, updated twice a second.
        fpsFrames++;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double dt = (double)(now.QuadPart - fpsT0.QuadPart) / qpf.QuadPart;
        if (dt >= 0.5) {
            char title[96];
            snprintf(title, sizeof title, "Human History - %s - %.0f fps", simDate().c_str(), fpsFrames / dt);
            SetWindowTextA(hwnd, title);
            fpsFrames = 0;
            fpsT0 = now;
        }
    }

    if (app.genThread.joinable()) app.genThread.join();
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(rc);
    ReleaseDC(hwnd, dc);
    DestroyWindow(hwnd);
    return 0;
}
