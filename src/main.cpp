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

// ---------------------------------------------------------------- app state

struct App {
    Camera cam;
    bool dragging = false;
    bool anchorValid = false;
    Vec3 anchor{}; // surface point grabbed at mouse-down
    int lastX = 0, lastY = 0;
    GLuint program = 0;
    bool running = true;
};
static App app;

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

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        app.cam.width = std::max(1, (int)LOWORD(lp));
        app.cam.height = std::max(1, (int)HIWORD(lp));
        app.cam.clampAltitude();
        glViewport(0, 0, app.cam.width, app.cam.height);
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
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
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        double factor = std::pow(0.8, delta / (double)WHEEL_DELTA);
        app.cam.altitude *= factor;
        app.cam.clampAltitude();
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) app.running = false;
        return 0;
    case WM_CLOSE:
        app.running = false;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- main

int main() {
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
    HWND hwnd = CreateWindowA("IronAndBlood", "Iron and Blood", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                              nullptr, nullptr, inst, nullptr);
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
    printf("OpenGL %s\n", (const char*)glGetString(GL_VERSION));
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

    app.cam.clampAltitude();

    while (app.running) {
        MSG m;
        while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
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
        glDrawArrays(GL_TRIANGLES, 0, 3);
        SwapBuffers(dc);
    }

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(rc);
    ReleaseDC(hwnd, dc);
    DestroyWindow(hwnd);
    return 0;
}
