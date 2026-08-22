// Iron and Blood — version 1: view the globe, zoom, pan.
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
#include <random>

// ---------------------------------------------------------------- GL loading

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82

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
typedef void(APIENTRY* PFNGLUNIFORM1IPROC)(GLint, GLint);
typedef void(APIENTRY* PFNGLUNIFORMMATRIX3FVPROC)(GLint, GLsizei, GLboolean, const GLfloat*);
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
static PFNGLUNIFORM1IPROC glUniform1i;
static PFNGLUNIFORMMATRIX3FVPROC glUniformMatrix3fv;
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
    ok &= load(glUniform1i, "glUniform1i");
    ok &= load(glUniformMatrix3fv, "glUniformMatrix3fv");
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

// A world is a seed plus where the camera was left. The seed rotates and
// offsets the terrain noise so every seed is a different globe.
struct World {
    uint32_t seed = 0;
    std::string name;
    float rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // column-major mat3 for GL
    Vec3 offset{};

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
        if (name.empty()) name = "world-" + std::to_string(seed);
    }
};

static std::string worldsDir() { return exeDir() + "\\worlds"; }

static bool saveWorld(const World& w, const Camera& c) {
    CreateDirectoryA(worldsDir().c_str(), nullptr);
    std::ofstream f(worldsDir() + "\\" + w.name + ".ibw");
    if (!f) return false;
    f.precision(17);
    f << "seed " << w.seed << "\n";
    f << "lat " << c.lat << "\n";
    f << "lon " << c.lon << "\n";
    f << "altitude " << c.altitude << "\n";
    return (bool)f;
}

static bool loadWorld(const std::string& name, World& w, Camera& c) {
    std::ifstream f(worldsDir() + "\\" + name + ".ibw");
    if (!f) return false;
    w = World{};
    w.name = name;
    std::string key;
    while (f >> key) {
        if (key == "seed") f >> w.seed;
        else if (key == "lat") f >> c.lat;
        else if (key == "lon") f >> c.lon;
        else if (key == "altitude") f >> c.altitude;
        else { std::string skip; f >> skip; }
    }
    w.derive();
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

enum class Screen { MainMenu, LoadMenu, InGame, PauseMenu };

// Control IDs for the Win32 controls that make up the menus.
enum : int {
    ID_NEW_WORLD = 100, ID_LOAD_WORLD, ID_QUIT,
    ID_LOAD_LIST, ID_LOAD_CONFIRM, ID_LOAD_DELETE, ID_LOAD_BACK,
    ID_SAVE_NAME, ID_SAVE_WORLD, ID_MAIN_MENU, ID_PAUSE_QUIT,
    ID_TITLE, ID_STATUS,
};

struct App {
    Camera cam;
    World world;
    Screen screen = Screen::MainMenu;
    bool dragging = false;
    bool anchorValid = false;
    Vec3 anchor{}; // surface point grabbed at mouse-down
    int lastX = 0, lastY = 0;
    GLuint program = 0;
    bool running = true;
    HWND hwnd = nullptr;
    HFONT font = nullptr, titleFont = nullptr;
    HBRUSH bgBrush = nullptr;
    std::vector<std::pair<int, HWND>> controls;
};
static App app;

static HWND control(int id) {
    for (auto& c : app.controls)
        if (c.first == id) return c.second;
    return nullptr;
}

static void setStatus(const std::string& s) { SetWindowTextA(control(ID_STATUS), s.c_str()); }

static void addControl(int id, const char* cls, const char* text, DWORD style) {
    HWND h = CreateWindowA(cls, text, WS_CHILD | style, 0, 0, 10, 10, app.hwnd, (HMENU)(INT_PTR)id,
                           GetModuleHandleA(nullptr), nullptr);
    SendMessageA(h, WM_SETFONT, (WPARAM)(id == ID_TITLE ? app.titleFont : app.font), TRUE);
    app.controls.push_back({id, h});
}

static void createControls() {
    app.font = CreateFontA(24, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    app.titleFont = CreateFontA(56, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    app.bgBrush = CreateSolidBrush(RGB(8, 8, 16));
    addControl(ID_TITLE, "STATIC", "Iron and Blood", SS_CENTER);
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
    case Screen::InGame:
        break;
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

static void setScreen(Screen s) {
    app.screen = s;
    app.dragging = false;
    if (s == Screen::LoadMenu) refreshWorldList();
    if (s == Screen::PauseMenu) SetWindowTextA(control(ID_SAVE_NAME), app.world.name.c_str());
    layoutControls();
    if (s == Screen::InGame) SetFocus(app.hwnd);
    if (s == Screen::PauseMenu) {
        HWND edit = control(ID_SAVE_NAME);
        SetFocus(edit);
        SendMessageA(edit, EM_SETSEL, 0, -1);
    }
}

static void newWorld() {
    app.world = World{};
    app.world.seed = (uint32_t)std::random_device{}();
    app.world.derive();
    app.cam.lat = 0.35;
    app.cam.lon = 0.0;
    app.cam.altitude = app.cam.maxAltitude();
    setStatus("");
    setScreen(Screen::InGame);
}

static void onCommand(int id) {
    switch (id) {
    case ID_NEW_WORLD: newWorld(); break;
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
        if (loadWorld(name, app.world, app.cam)) {
            setStatus("");
            setScreen(Screen::InGame);
        } else {
            setStatus("Could not load " + name);
        }
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
    case ID_MAIN_MENU:
        setStatus("");
        setScreen(Screen::MainMenu);
        break;
    }
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
    case Screen::LoadMenu: setScreen(Screen::MainMenu); break;
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
        app.anchorValid = app.cam.hitSphere(app.lastX, app.lastY, app.anchor);
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        app.dragging = false;
        return 0;
    case WM_MOUSEMOVE:
        if (app.dragging) {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            applyDrag(x, y);
            app.lastX = x;
            app.lastY = y;
        }
        return 0;
    case WM_MOUSEWHEEL: {
        if (app.screen != Screen::InGame) return 0;
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        double factor = std::pow(0.8, delta / (double)WHEEL_DELTA);
        app.cam.altitude *= factor;
        app.cam.clampAltitude();
        return 0;
    }
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
    wc.lpszClassName = "IronAndBlood";
    RegisterClassA(&wc);

    RECT r = {0, 0, app.cam.width, app.cam.height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    app.hwnd = CreateWindowA("IronAndBlood", "Iron and Blood",
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

    createControls();
    app.cam.clampAltitude();

    // Testing shortcut: ironblood <latDeg> <lonDeg> [altitudeKm] [seed] skips the menu.
    if (argc >= 3) {
        app.world.seed = argc >= 5 ? (uint32_t)strtoul(argv[4], nullptr, 10) : 0;
        app.world.derive();
        app.cam.lat = atof(argv[1]) * PI / 180;
        app.cam.lon = atof(argv[2]) * PI / 180;
        if (argc >= 4) app.cam.altitude = atof(argv[3]) / EARTH_RADIUS_KM;
        app.cam.clampAltitude();
        setScreen(Screen::InGame);
    } else {
        setScreen(Screen::MainMenu);
    }

    while (app.running) {
        MSG m;
        while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
            // Buttons take keyboard focus, so catch Escape before it reaches them.
            if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) {
                onEscape();
                continue;
            }
            if (m.message == WM_KEYDOWN && m.wParam == VK_RETURN && app.screen == Screen::PauseMenu) {
                onCommand(ID_SAVE_WORLD);
                continue;
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
            octaves = std::clamp(octaves, 4, 20);

            glUniform3f(uCamPos, (float)p.x, (float)p.y, (float)p.z);
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
            glDrawArrays(GL_TRIANGLES, 0, 3);
        } else {
            glClearColor(8 / 255.f, 8 / 255.f, 16 / 255.f, 1);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        SwapBuffers(dc);
    }

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(rc);
    ReleaseDC(hwnd, dc);
    DestroyWindow(hwnd);
    return 0;
}
