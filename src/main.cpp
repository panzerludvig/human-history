// Human History — version 1: view the globe, zoom, pan.
// Win32 + OpenGL, no external dependencies. The whole globe is raycast and
// shaded procedurally in shaders/globe.frag; this file owns the window,
// the camera, and input.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <unordered_map>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <random>
#include <thread>
#include <atomic>
#include "terrain.h"
#include "hydrology.h"
#include "population.h"
#include "technology.h"
#include "sim.h"
#include "atmosphere.h"
#include "gl.h"
#include "camera.h"
#include "world.h"
#include "savefile.h"
#include "inspect.h"
#include "bmp.h"
#include "textures.h"

// Generation-stage feedback on the menu status line. The build runs on the
// UI thread, so the label is repainted synchronously.
static void buildProgress(const char* stage);

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
    camera::Camera cam;
    world::World world;
    Screen screen = Screen::MainMenu;
    bool dragging = false;
    camera::Drag drag;
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
    textures::State tex;
    // The marker overlay: a screen-sized image drawn with GDI and laid over
    // the globe by the shader. Magenta means "nothing here".
    HDC ovDC = nullptr;
    HBITMAP ovBmp = nullptr;
    unsigned char* ovBits = nullptr;
    int ovW = 0, ovH = 0;
    GLuint ovTex = 0;
    HFONT markerFont = nullptr, markerBold = nullptr, markBold = nullptr;
    // Where each event chip was drawn, so a click can find it. Rebuilt with
    // the overlay; a chip belongs to a settlement or to a band, never both.
    struct MarkHit { int x, y, w, h; uint32_t sid, bandId; };
    std::vector<MarkHit> markHits;
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
    HWND news = nullptr;   // the feed down the right-hand side
    int newsLevel = 0;     // 0 kinds, 1 the entries of one kind, 2 one entry
    int newsKind = 0;      // which kind is open
    int newsPick = 0;      // which entry is open
    int newsScroll = 0;
    bool newsOpen = true;  // collapsed to a tab on the right edge when false
    std::vector<std::pair<int, HWND>> controls;
};
static App app;

// ------------------------------------------------------- marker overlay
//
// Close up, a settlement is its houses and a band is its people, both drawn
// on the ground by the shader. From further off that is a smear of pixels,
// so the map takes over: a round marker with a hut in it for a settlement, a
// labelled rectangle for a band, and the name underneath until the view is
// wide enough that names would be a thicket. Markers and names are drawn
// here with GDI into a screen-sized image and laid over the globe by the
// shader -- pixel work belongs in pixels, and it puts the same font on the
// map as on the panels.
constexpr double NAME_KMPP = 1.5;    // wider views than this drop the names
constexpr int THIN_PX = 22;          // markers stand at least this far apart
constexpr double HUT_KMPP = 0.004;   // closer than this the shader draws houses
constexpr double WALK_KMPP = 0.0006; // and closer than this, the people in a band

// The marker shrinks as the view widens: it is a pin at local range and a
// dot at continental range, where what matters is where people are thick on
// the ground rather than which place is which.
//
// It changes character where the names do, so a settlement and a band say
// the same amount about themselves at any given range: while there are
// names the marker is big enough to hold its hut (6 px and up), and beyond
// them it stops being a symbol sized for the eye and becomes a fixed size on
// the ground, shrinking with the view rather than swelling to cover a
// province. It bottoms out at two pixels, near enough the smallest band
// marker that a village does not look like less than a walking party. Since
// the markers no longer grow they no longer crowd each other out, which is
// what made them wink away a few at a time while zooming out.
static int markerRadius(double kmpp) {
    if (kmpp < NAME_KMPP)
        return std::clamp((int)std::lround(8.0 - std::log10(kmpp / HUT_KMPP)), 6, 8);
    double fixedKm = 6.0 * NAME_KMPP; // its size on the ground, from the handover
    return std::max(2, (int)std::lround(fixedKm / kmpp));
}


// ------------------------------------------------------------ event marks
//
// What happened to a place this turn, drawn as a row of chips above its
// marker: a family-coloured square with a cream glyph cut out of it, eleven
// pixels across. The chip is what makes it readable -- a bare glyph over an
// ice sheet is invisible -- and the colour sorts the event before the glyph
// is even looked at. Marks last exactly as long as the news feed does: they
// are the same events, cleared at the start of every step.
//
// Every glyph is filled polygons on an 11x11 grid: no curves, no thin
// diagonals, nothing that needs a second pixel to read. Coordinates below
// match the proposal drawing exactly.
struct MarkPoly { const float* xy; int n; };
struct MarkGlyph { const MarkPoly* fill; int nf; const MarkPoly* cut; int nc; };

// clang-format off
static const float MP_RELOCATE[] = {1,4, 6,4, 6,2, 10,5.5f, 6,9, 6,7, 1,7};
static const float MP_SPLIT_A[]  = {0.4f,4.7f, 4.6f,4.7f, 4.6f,6.3f, 0.4f,6.3f};
static const float MP_SPLIT_B[]  = {3.5f,4.9f, 4.6f,6.1f, 8.2f,2.9f, 7.1f,1.7f};
static const float MP_SPLIT_C[]  = {6.3f,1.5f, 10.2f,0.6f, 9.2f,4.4f};
static const float MP_SPLIT_D[]  = {3.5f,6.1f, 4.6f,4.9f, 8.2f,8.1f, 7.1f,9.3f};
static const float MP_SPLIT_E[]  = {6.3f,9.5f, 10.2f,10.4f, 9.2f,6.6f};
static const float MP_SET_A[]    = {2,9, 9,9, 9,10, 2,10};
static const float MP_SET_B[]    = {5.5f,1, 10,5, 1,5};
static const float MP_SET_C[]    = {2,5, 9,5, 9,8, 2,8};
static const float MP_FOUND_A[]  = {5.5f,2, 10,6, 1,6};
static const float MP_FOUND_B[]  = {2,6, 9,6, 9,10, 2,10};
static const float MP_FOUND_C[]  = {0,1, 2,0, 3,2, 1,3};
static const float MP_MERGE_A[]  = {1.0f,1.4f, 2.1f,0.4f, 5.6f,4.1f, 4.5f,5.1f};
static const float MP_MERGE_B[]  = {1.0f,9.6f, 2.1f,10.6f, 5.6f,6.9f, 4.5f,5.9f};
static const float MP_MERGE_C[]  = {4.2f,4.7f, 8.0f,4.7f, 8.0f,6.3f, 4.2f,6.3f};
static const float MP_MERGE_D[]  = {7.4f,2.6f, 10.8f,5.5f, 7.4f,8.4f};
static const float MP_DIED_A[]   = {4,1, 7,1, 7,10, 4,10};
static const float MP_DIED_B[]   = {1,3, 10,3, 10,5, 1,5};
static const float MP_BLADE[]    = {2.1f,9.6f, 3.3f,8.4f, 10.4f,1.3f, 10.9f,0.2f, 9.6f,0.6f, 2.6f,7.7f};
static const float MP_BLADE2[]   = {8.9f,9.6f, 7.7f,8.4f, 0.6f,1.3f, 0.1f,0.2f, 1.4f,0.6f, 8.4f,7.7f};
static const float MP_GUARD[]    = {1.0f,7.3f, 1.9f,6.4f, 4.6f,9.1f, 3.7f,10.0f};
static const float MP_GUARD2[]   = {10.0f,7.3f, 9.1f,6.4f, 6.4f,9.1f, 7.3f,10.0f};
static const float MP_POMMEL[]   = {0.4f,9.0f, 1.6f,7.8f, 3.2f,9.4f, 2.0f,10.6f};
static const float MP_SHIELD[]   = {1,1, 10,1, 10,5, 5.5f,10, 1,5};
static const float MP_HOME[]     = {10,4, 5,4, 5,2, 1,5.5f, 5,9, 5,7, 10,7};
static const float MP_STAR[]     = {5.5f,0, 7,4, 11,5.5f, 7,7, 5.5f,11, 4,7, 0,5.5f, 4,4};
static const float MP_STAR_S[]   = {5.5f,0, 6.6f,3, 9.5f,4, 6.6f,5, 5.5f,8, 4.4f,5, 1.5f,4, 4.4f,3};
static const float MP_DOWN[]     = {4.5f,7, 6.5f,7, 6.5f,9, 8,9, 5.5f,11, 3,9, 4.5f,9};
static const float MP_GRAN_A[]   = {2,1, 9,1, 10,4, 1,4};
static const float MP_GRAN_B[]   = {2,4, 9,4, 9,8, 2,8};
static const float MP_GRAN_C[]   = {2,8, 3.5f,8, 3.5f,11, 2,11};
static const float MP_GRAN_D[]   = {7.5f,8, 9,8, 9,11, 7.5f,11};
static const float MP_BONE_A[]   = {0.6f,10.4f, 1.6f,11.2f, 10.4f,6.6f, 9.4f,5.8f};
static const float MP_BONE_B[]   = {0.6f,6.6f, 1.6f,5.8f, 10.4f,10.4f, 9.4f,11.2f};
static const float MP_KNOB_A[]   = {0.0f,10.0f, 1.4f,9.3f, 2.0f,10.6f, 0.6f,11.3f};
static const float MP_KNOB_B[]   = {9.0f,5.7f, 10.4f,5.0f, 11.0f,6.3f, 9.6f,7.0f};
static const float MP_KNOB_C[]   = {0.0f,6.3f, 1.4f,7.0f, 2.0f,5.7f, 0.6f,5.0f};
static const float MP_KNOB_D[]   = {9.0f,10.6f, 10.4f,11.3f, 11.0f,10.0f, 9.6f,9.3f};
static const float MP_SKULL[]    = {2.6f,1.2f, 3.6f,0.2f, 7.4f,0.2f, 8.4f,1.2f, 8.4f,5.4f, 2.6f,5.4f};
static const float MP_JAW[]      = {3.7f,5.4f, 7.3f,5.4f, 7.3f,7.4f, 3.7f,7.4f};
static const float MP_SLASH[]    = {0.6f,9.4f, 2.0f,10.8f, 10.4f,2.4f, 9.0f,1.0f};
static const float MP_EYE_L[]    = {3.5f,2.0f, 5.0f,2.0f, 5.0f,4.0f, 3.5f,4.0f};
static const float MP_EYE_R[]    = {6.0f,2.0f, 7.5f,2.0f, 7.5f,4.0f, 6.0f,4.0f};
static const float MP_NOSE[]     = {5.0f,4.4f, 6.0f,4.4f, 6.0f,5.4f, 5.0f,5.4f};
static const float MP_TOOTH_L[]  = {4.6f,6.2f, 5.1f,6.2f, 5.1f,7.4f, 4.6f,7.4f};
static const float MP_TOOTH_R[]  = {5.9f,6.2f, 6.4f,6.2f, 6.4f,7.4f, 5.9f,7.4f};

#define MPOLY(a) {a, (int)(sizeof(a) / sizeof(float) / 2)}
static const MarkPoly MG_RELOCATE[] = {MPOLY(MP_RELOCATE)};
static const MarkPoly MG_SPLIT[]    = {MPOLY(MP_SPLIT_A), MPOLY(MP_SPLIT_B), MPOLY(MP_SPLIT_C),
                                       MPOLY(MP_SPLIT_D), MPOLY(MP_SPLIT_E)};
static const MarkPoly MG_SETTLED[]  = {MPOLY(MP_SET_A), MPOLY(MP_SET_B), MPOLY(MP_SET_C)};
static const MarkPoly MG_FOUNDED[]  = {MPOLY(MP_FOUND_A), MPOLY(MP_FOUND_B), MPOLY(MP_FOUND_C)};
static const MarkPoly MG_MERGED[]   = {MPOLY(MP_MERGE_A), MPOLY(MP_MERGE_B), MPOLY(MP_MERGE_C),
                                       MPOLY(MP_MERGE_D)};
static const MarkPoly MG_PERISHED[] = {MPOLY(MP_DIED_A), MPOLY(MP_DIED_B)};
static const MarkPoly MG_RAIDOUT[]  = {MPOLY(MP_BLADE), MPOLY(MP_GUARD), MPOLY(MP_POMMEL)};
static const MarkPoly MG_RAIDHIT[]  = {MPOLY(MP_BLADE), MPOLY(MP_BLADE2), MPOLY(MP_GUARD),
                                       MPOLY(MP_GUARD2)};
static const MarkPoly MG_RAIDHELD[] = {MPOLY(MP_SHIELD)};
static const MarkPoly MG_RAIDHOME[] = {MPOLY(MP_HOME)};
static const MarkPoly MG_INVENTED[] = {MPOLY(MP_STAR)};
static const MarkPoly MG_ADOPTED[]  = {MPOLY(MP_STAR_S), MPOLY(MP_DOWN)};
static const MarkPoly MG_GRANARY[]  = {MPOLY(MP_GRAN_A), MPOLY(MP_GRAN_B), MPOLY(MP_GRAN_C),
                                       MPOLY(MP_GRAN_D)};
static const MarkPoly MG_GAME[]     = {MPOLY(MP_BONE_A), MPOLY(MP_BONE_B), MPOLY(MP_KNOB_A),
                                       MPOLY(MP_KNOB_B), MPOLY(MP_KNOB_C), MPOLY(MP_KNOB_D),
                                       MPOLY(MP_SKULL), MPOLY(MP_JAW)};
static const MarkPoly MG_GAME_CUT[] = {MPOLY(MP_EYE_L), MPOLY(MP_EYE_R), MPOLY(MP_NOSE),
                                       MPOLY(MP_TOOTH_L), MPOLY(MP_TOOTH_R)};
static const MarkPoly MG_LOST[]     = {MPOLY(MP_STAR)};
static const MarkPoly MG_LOST_CUT[] = {MPOLY(MP_SLASH)};
#undef MPOLY

#define MGLYPH(a) {a, (int)(sizeof(a) / sizeof(MarkPoly)), nullptr, 0}
// Indexed by population::EV_*, in that order.
static const MarkGlyph MARK_GLYPH[population::EV_KINDS] = {
    MGLYPH(MG_RELOCATE), MGLYPH(MG_SPLIT),    MGLYPH(MG_SETTLED),  MGLYPH(MG_FOUNDED),
    MGLYPH(MG_MERGED),   MGLYPH(MG_PERISHED), MGLYPH(MG_RAIDOUT),  MGLYPH(MG_RAIDHIT),
    MGLYPH(MG_RAIDHELD), MGLYPH(MG_RAIDHOME), MGLYPH(MG_INVENTED), MGLYPH(MG_ADOPTED),
    MGLYPH(MG_GRANARY),
    {MG_GAME, (int)(sizeof(MG_GAME) / sizeof(MarkPoly)), MG_GAME_CUT,
     (int)(sizeof(MG_GAME_CUT) / sizeof(MarkPoly))},
    // A technology lost: the star of its invention, struck through.
    {MG_LOST, (int)(sizeof(MG_LOST) / sizeof(MarkPoly)), MG_LOST_CUT,
     (int)(sizeof(MG_LOST_CUT) / sizeof(MarkPoly))},
};
#undef MGLYPH
// clang-format on

// Five families. Violence first and the emptied land second, so a settlement
// that was raided always shows the raid: the count chip absorbs granaries,
// never the fighting.
static int markFamily(int kind) {
    switch (kind) {
    case population::EV_RAID_LAUNCH:
    case population::EV_RAID_HIT:
    case population::EV_RAID_HELD:
    case population::EV_RAID_HOME: return 0; // violence
    case population::EV_GAME_GONE: return 1; // the land
    case population::EV_INVENTED:
    case population::EV_ADOPTED:
    case population::EV_TECH_LOST: return 3; // knowledge
    case population::EV_GRANARY: return 4;   // building
    default: return 2;                       // movement
    }
}
static COLORREF markColour(int family) {
    static const COLORREF c[5] = {RGB(163, 53, 42), RGB(107, 122, 74), RGB(232, 176, 66),
                                  RGB(216, 194, 90), RGB(185, 146, 90)};
    return c[family < 0 || family > 4 ? 2 : family];
}

constexpr int CHIP = 11;      // a mark, square
constexpr int CHIP_GAP = 1;   // and the air between two of them
constexpr int CHIP_SHOWN = 4; // before the rest become a number

static void overlayEnsure() {
    if (app.ovDC && app.ovW == app.cam.width && app.ovH == app.cam.height) return;
    if (app.ovBmp) { DeleteObject(app.ovBmp); app.ovBmp = nullptr; }
    if (app.ovDC) { DeleteDC(app.ovDC); app.ovDC = nullptr; }
    app.ovW = app.cam.width;
    app.ovH = app.cam.height;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = app.ovW;
    bi.bmiHeader.biHeight = app.ovH; // bottom-up, so rows arrive in OpenGL order
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    app.ovDC = CreateCompatibleDC(screen);
    app.ovBmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, (void**)&app.ovBits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    SelectObject(app.ovDC, app.ovBmp);
    SetBkMode(app.ovDC, TRANSPARENT);
}

// One mark: the family square, a dark edge, and the glyph cut out in cream.
static void drawChip(HDC dc, int kind, int x, int y, HBRUSH edgeBr, HBRUSH creamBr) {
    RECT r{x, y, x + CHIP, y + CHIP};
    FillRect(dc, &r, edgeBr);
    HBRUSH fam = CreateSolidBrush(markColour(markFamily(kind)));
    RECT in{x + 1, y + 1, x + CHIP - 1, y + CHIP - 1};
    FillRect(dc, &in, fam);
    const MarkGlyph& g = MARK_GLYPH[kind];
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    const float s = (CHIP - 2) / 11.0f;
    auto run = [&](const MarkPoly* polys, int n, HBRUSH br) {
        HGDIOBJ old = SelectObject(dc, br);
        for (int i = 0; i < n; i++) {
            POINT pt[16];
            int m = polys[i].n < 16 ? polys[i].n : 16;
            for (int k = 0; k < m; k++) {
                pt[k].x = x + 1 + (LONG)std::lround(polys[i].xy[k * 2] * s);
                pt[k].y = y + 1 + (LONG)std::lround(polys[i].xy[k * 2 + 1] * s);
            }
            Polygon(dc, pt, m);
        }
        SelectObject(dc, old);
    };
    run(g.fill, g.nf, creamBr);
    if (g.nc) run(g.cut, g.nc, fam); // the skull needs its sockets back
    SelectObject(dc, oldPen);
    DeleteObject(fam);
}

// The overflow: how many marks are not shown. The only chip that holds type.
static void drawCountChip(HDC dc, int n, int x, int y, HBRUSH edgeBr) {
    RECT r{x, y, x + CHIP, y + CHIP};
    FillRect(dc, &r, edgeBr);
    HBRUSH br = CreateSolidBrush(RGB(74, 64, 52));
    RECT in{x + 1, y + 1, x + CHIP - 1, y + CHIP - 1};
    FillRect(dc, &in, br);
    DeleteObject(br);
    char t[8];
    snprintf(t, sizeof t, "%d", n > 99 ? 99 : n);
    SelectObject(dc, app.markBold);
    UINT old = SetTextAlign(dc, TA_CENTER | TA_TOP);
    SetTextColor(dc, RGB(238, 230, 208));
    TextOutA(dc, x + CHIP / 2, y, t, (int)strlen(t));
    SetTextAlign(dc, old);
}

// A house seen from the side, sized to sit inside a marker of radius r.
static void drawHutGlyph(HDC dc, int cx, int cy, int r) {
    int w = r * 5 / 9, hgt = r * 6 / 9;
    POINT roof[3] = {{cx - w - 1, cy - 1}, {cx, cy - hgt}, {cx + w + 1, cy - 1}};
    Polygon(dc, roof, 3);
    Rectangle(dc, cx - w + 2, cy - 1, cx + w - 1, cy + hgt - 1);
}

// One line of text centred on x, with a dark copy under it so a name stays
// legible over snow as well as over forest.
static void drawLabel(HDC dc, int x, int y, const char* txt) {
    int n = (int)strlen(txt);
    SetTextColor(dc, RGB(24, 20, 14));
    TextOutA(dc, x + 1, y + 1, txt, n);
    SetTextColor(dc, RGB(250, 246, 234));
    TextOutA(dc, x, y, txt, n);
}

// What happened to each settlement and each band this turn, ordered so the
// most telling marks survive the cut: family first (violence, then the land,
// then movement, knowledge, building), and within a family the most recent.
// A raid launched belongs to the settlement that sent the party out, not to
// the party -- the decision was the settlement's.
static void gatherMarks(std::unordered_map<uint32_t, std::vector<int>>& bySite,
                        std::unordered_map<uint32_t, std::vector<int>>& byBand) {
    const population::Field& pf = app.world.pop;
    for (size_t i = pf.events.size(); i-- > 0;) { // newest first
        const population::Event& e = pf.events[i];
        if (e.sid) bySite[e.sid].push_back(e.kind);
        if (e.bandId && e.kind != population::EV_RAID_LAUNCH) byBand[e.bandId].push_back(e.kind);
    }
    auto sortFamily = [](std::unordered_map<uint32_t, std::vector<int>>& m) {
        for (auto& kv : m)
            std::stable_sort(kv.second.begin(), kv.second.end(),
                             [](int a, int b) { return markFamily(a) < markFamily(b); });
    };
    sortFamily(bySite);
    sortFamily(byBand);
}

// A row of marks above a marker, centred on it: four of them, then a count
// of the rest. Records where each chip landed so a click can find it.
static void drawMarkRow(HDC dc, const std::vector<int>& kinds, int cx, int bottomY, uint32_t sid,
                        uint32_t bandId, HBRUSH edgeBr, HBRUSH creamBr) {
    if (kinds.empty()) return;
    int shown = (int)kinds.size() <= CHIP_SHOWN ? (int)kinds.size() : CHIP_SHOWN;
    int hidden = (int)kinds.size() - shown;
    int n = shown + (hidden > 0 ? 1 : 0);
    int w = n * CHIP + (n - 1) * CHIP_GAP;
    int x = cx - w / 2, y = bottomY - CHIP;
    for (int i = 0; i < shown; i++) {
        drawChip(dc, kinds[i], x, y, edgeBr, creamBr);
        app.markHits.push_back({x, y, CHIP, CHIP, sid, bandId});
        x += CHIP + CHIP_GAP;
    }
    if (hidden > 0) {
        drawCountChip(dc, hidden, x, y, edgeBr);
        app.markHits.push_back({x, y, CHIP, CHIP, sid, bandId});
    }
}

static void paintOverlay() {
    overlayEnsure();
    app.markHits.clear();
    if (!app.ovDC) return;
    HDC dc = app.ovDC;
    RECT full{0, 0, app.ovW, app.ovH};
    HBRUSH clear = CreateSolidBrush(RGB(255, 0, 255)); // "nothing here", to the shader
    FillRect(dc, &full, clear);
    DeleteObject(clear);
    if (app.screen != Screen::InGame || app.world.pop.settlements.empty()) return;
    double kmpp = app.cam.kmPerPixel();
    // Close up the shader draws the houses and the walking people
    // themselves, so the overlay adds only the names.
    bool ground = kmpp < HUT_KMPP;      // houses are being drawn on the ground
    bool walking = kmpp < WALK_KMPP;    // and the people of a band, one by one
    bool names = kmpp < NAME_KMPP;
    int mr = markerRadius(kmpp);
    // Names need room; bare markers only need to keep off each other, and
    // ground-sized ones barely touch, so the thinning all but stops.
    double spacingKm = (names ? std::max(2 * mr + 6, THIN_PX) : std::max(2 * mr + 2, 4)) * kmpp;

    // Thinning: a thousand settlements in view is a legible map only if the
    // small ones give way to the large. The squares are on the GROUND, not on
    // the screen -- a screen grid moves with the camera, so panning kept
    // changing which settlement won its square and markers blinked in and out
    // as you dragged. Ground squares are the same wherever you are looking,
    // and their size steps in powers of two so a slow zoom does not churn
    // them either.
    double stepDeg = std::pow(2.0, std::round(std::log2(spacingKm / 111.32)));
    struct Cand { float x, y, w; int idx; bool band; };
    std::vector<Cand> cands;
    std::unordered_map<long long, int> best;
    auto offer = [&](float x, float y, float w, int idx, bool band, const terrain::V3& at) {
        if (x < -60 || y < -30 || x > app.ovW + 60 || y > app.ovH + 30) return;
        double latDeg = std::asin(std::clamp(at.z, -1.0f, 1.0f)) * 180 / camera::PI;
        double lonDeg = std::atan2(at.y, at.x) * 180 / camera::PI;
        long long li = (long long)std::floor(latDeg / stepDeg);
        // Columns narrow towards the poles, so the longitude step widens with
        // the band's own latitude -- the band's, not the marker's, or two
        // neighbours would land in overlapping grids.
        double bandLat = (li + 0.5) * stepDeg * camera::PI / 180;
        double lonStep = stepDeg / std::max(std::cos(bandLat), 0.02);
        long long ci = (long long)std::floor(lonDeg / lonStep);
        cands.push_back({x, y, w, idx, band});
        int& b = best[li * 1000003LL + ci];
        if (!b || cands[b - 1].w < w) b = (int)cands.size();
    };
    const population::Field& pf = app.world.pop;
    float sx, sy;
    for (size_t i = 0; i < pf.settlements.size(); i++) {
        terrain::V3 at = sim::cellCentre(pf.settlements[i].cell);
        if (camera::projectToScreen(app.cam, {at.x, at.y, at.z}, sx, sy))
            offer(sx, sy, pf.settlements[i].P, (int)i, false, at);
    }
    // Bands outrank settlements for the square they stand on: they are the
    // thing that moves, and losing one to a village it is passing is worse
    // than losing the village.
    for (size_t i = 0; i < pf.bands.size(); i++) {
        const population::Band& b = pf.bands[i];
        terrain::V3 at{b.px, b.py, b.pz};
        if (camera::projectToScreen(app.cam, {at.x, at.y, at.z}, sx, sy))
            offer(sx, sy, b.P + 1e6f, (int)i, true, at);
    }

    std::unordered_map<uint32_t, std::vector<int>> markSite, markBand;
    gatherMarks(markSite, markBand);
    HBRUSH cream = CreateSolidBrush(RGB(238, 230, 208));
    HBRUSH amber = CreateSolidBrush(RGB(232, 176, 66));
    HBRUSH ink = CreateSolidBrush(RGB(96, 52, 28));
    HPEN edge = CreatePen(PS_SOLID, 1, RGB(40, 28, 18));
    HBRUSH edgeFill = CreateSolidBrush(RGB(40, 28, 18)); // the chip's dark rim
    // Under a few pixels the outline is the whole marker, so it goes and the
    // fill speaks for itself.
    HGDIOBJ oldPen = SelectObject(dc, mr <= 2 ? GetStockObject(NULL_PEN) : (HGDIOBJ)edge);
    SetTextAlign(dc, TA_CENTER | TA_TOP);
    for (const auto& kv : best) {
        const Cand& c = cands[kv.second - 1];
        int cx = (int)std::lround(c.x), cy = (int)std::lround(c.y);
        if (!c.band) {
            const population::Settlement& st = pf.settlements[c.idx];
            int below = mr + 2;
            if (ground) {
                below = std::min((int)(sim::fieldInnerKm(st.P) / kmpp), 60) + 3;
            } else {
                SelectObject(dc, mr >= 6 ? cream : ink);
                // A two-pixel circle is a rectangle anyway, and there can be
                // thousands of them once the whole world is in view.
                if (mr <= 2) Rectangle(dc, cx - mr, cy - mr, cx + mr + 1, cy + mr + 1);
                else Ellipse(dc, cx - mr, cy - mr, cx + mr + 1, cy + mr + 1);
                if (mr >= 6) { // the hut, wherever a band would still name itself
                    SelectObject(dc, ink);
                    drawHutGlyph(dc, cx, cy, mr);
                }
            }
            if (names) {
                SelectObject(dc, app.markerFont);
                drawLabel(dc, cx, cy + below, st.name);
            }
            auto it = markSite.find(st.id);
            if (it != markSite.end() && !ground)
                drawMarkRow(dc, it->second, cx, cy - mr - 4, st.id, 0, edgeFill, cream);
        } else {
            const population::Band& b = pf.bands[c.idx];
            const char* kind = b.purpose == population::BAND_RAID ? "Raiders"
                               : b.colonists                     ? "Colonists"
                                                                 : "Tribe";
            SelectObject(dc, app.markerBold);
            SIZE ts{};
            GetTextExtentPoint32A(dc, kind, (int)strlen(kind), &ts);
            int hw = (int)ts.cx / 2 + 5, hh = 9;
            if (!names) { hw = std::max(mr + 2, 3); hh = std::max(mr, 2); } // no room for the word
            int below = hh + 2;
            if (walking) {
                below = std::min((int)(sim::bandSpreadKm(b.P) / kmpp), 60) + 3;
            } else {
                SelectObject(dc, amber);
                Rectangle(dc, cx - hw, cy - hh, cx + hw, cy + hh);
                if (names) {
                    SetTextColor(dc, RGB(38, 26, 10));
                    TextOutA(dc, cx, cy - hh + 2, kind, (int)strlen(kind));
                }
            }
            if (names) {
                SelectObject(dc, app.markerFont);
                drawLabel(dc, cx, cy + below, b.name);
            }
            auto it = markBand.find(b.id);
            if (it != markBand.end() && !walking)
                drawMarkRow(dc, it->second, cx, cy - hh - 4, 0, b.id, edgeFill, cream);
        }
    }
    // Close up, a row of chips floating over the roofs would be a lie about
    // where things happened, so only one mark is drawn down here: a building
    // finished, standing over the building itself.
    if (ground) {
        for (const population::Settlement& st : pf.settlements) {
            auto it = markSite.find(st.id);
            if (it == markSite.end()) continue;
            int built = 0;
            for (int k : it->second) built += k == population::EV_GRANARY ? 1 : 0;
            for (int i = 0; i < built; i++) {
                int g = (int)(st.granaries + 0.5f) - 1 - i;
                if (g < 0 || g >= 8) continue;
                float gx, gy;
                terrain::V3 gp = sim::granaryPos(st.cell, g);
                if (!camera::projectToScreen(app.cam, {gp.x, gp.y, gp.z}, gx, gy)) continue;
                int px = (int)std::lround(gx), py = (int)std::lround(gy);
                drawChip(dc, population::EV_GRANARY, px - CHIP / 2, py - CHIP - 10, edgeFill, cream);
                app.markHits.push_back({px - CHIP / 2, py - CHIP - 10, CHIP, CHIP, st.id, 0});
                HGDIOBJ op = SelectObject(dc, edgeFill);
                PatBlt(dc, px, py - 10, 1, 8, PATCOPY); // a stem down to the store
                SelectObject(dc, op);
            }
        }
    }
    SelectObject(dc, oldPen);
    DeleteObject(cream);
    DeleteObject(amber);
    DeleteObject(ink);
    DeleteObject(edge);
    DeleteObject(edgeFill);
}

// The overlay is a function of the camera, the world and the window, so it
// only needs redrawing when one of those moves. Repainting and uploading it
// costs about 2.4 ms, which is a quarter of a frame to spend on a picture
// that usually has not changed.
static bool overlayStale() {
    static double la = 1e9, lo = 1e9, alt = 0, t = -1;
    static int w = 0, h = 0, ns = -1, nb = -1, scr = -1;
    const population::Field& pf = app.world.pop;
    bool same = la == app.cam.lat && lo == app.cam.lon && alt == app.cam.altitude &&
                w == app.cam.width && h == app.cam.height && t == app.world.simTime &&
                ns == (int)pf.settlements.size() && nb == (int)pf.bands.size() &&
                scr == (int)app.screen;
    if (same) return false;
    la = app.cam.lat;
    lo = app.cam.lon;
    alt = app.cam.altitude;
    w = app.cam.width;
    h = app.cam.height;
    t = app.world.simTime;
    ns = (int)pf.settlements.size();
    nb = (int)pf.bands.size();
    scr = (int)app.screen;
    return true;
}

static void uploadOverlay() {
    glActiveTexture(GL_TEXTURE7);
    if (!app.ovTex) {
        glGenTextures(1, &app.ovTex);
        glBindTexture(GL_TEXTURE_2D, app.ovTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_2D, app.ovTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, app.ovW, app.ovH, 0, 0x80E1 /*GL_BGRA*/,
                 GL_UNSIGNED_BYTE, app.ovBits);
    glActiveTexture(GL_TEXTURE0);
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
    // Marker text is drawn without antialiasing on purpose: the overlay has
    // no alpha channel -- the shader keys on magenta -- so blended edges
    // would fringe. Crisp small type also suits a map.
    app.markerFont = CreateFontA(15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                 NONANTIALIASED_QUALITY, 0, "Segoe UI");
    app.markerBold = CreateFontA(14, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                 NONANTIALIASED_QUALITY, 0, "Segoe UI");
    app.markBold = CreateFontA(11, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                               NONANTIALIASED_QUALITY, 0, "Segoe UI"); // the count in a chip
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
    addControl(ID_GEN_SEED, "EDIT", "", WS_BORDER | ES_AUTOHSCROLL); // not ES_NUMBER: "earth" is a seed
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
    for (auto& n : savefile::list()) SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)n.c_str());
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

static void fillNewWorldFields(const world::World& w) {
    if (w.earth) SetWindowTextA(control(ID_GEN_SEED), "earth");
    else setEditNumber(ID_GEN_SEED, (double)w.seed);
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
    if (app.news) ShowWindow(app.news, s == Screen::InGame ? SW_SHOW : SW_HIDE);
    if (s == Screen::InGame) { updateDateLabel(); SetFocus(app.hwnd); }
    if (s == Screen::PauseMenu) {
        HWND edit = control(ID_SAVE_NAME);
        SetFocus(edit);
        SendMessageA(edit, EM_SETSEL, 0, -1);
    }
}

static uint32_t randomSeed() { return (uint32_t)std::random_device{}(); }

static void openNewWorldMenu() {
    app.world = world::World{};
    app.world.seed = randomSeed();
    setStatus("");
    setScreen(Screen::NewWorldMenu);
}

static void generateWorld() {
    world::World w;
    {
        // A seed reading "earth", in any case, is the template globe.
        char sb[64];
        GetWindowTextA(control(ID_GEN_SEED), sb, sizeof sb);
        std::string st = sb;
        for (char& ch : st) ch = (char)tolower((unsigned char)ch);
        w.earth = st.find("earth") != std::string::npos;
        w.seed = w.earth ? 1u : (uint32_t)std::clamp(atof(sb), 0.0, 4294967295.0);
    }
    w.landPercent = (float)std::clamp(getEditNumber(ID_GEN_LAND), 0.0, 100.0);
    w.concentration = (float)std::clamp(getEditNumber(ID_GEN_CONC), 0.0, 100.0);
    app.world = w;
    app.genKind = 0;
    app.genState = 1;
    app.genThread = std::thread([] {
        app.world.build(buildProgress);
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
    textures::uploadAll(app.tex, app.world);
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
            app.genOk = savefile::load(name, app.world, app.cam, buildProgress);
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
        std::string path = savefile::worldsDir() + "\\" + name + ".ibw";
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
        setStatus(savefile::save(app.world, app.cam) ? "Saved as " + name : "Save failed");
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

inline int newsWidth(); // defined with the news feed, below

static void updateTooltip(int x, int y) {
    HWND tip = control(ID_TOOLTIP);
    camera::Vec3 hit;
    if (app.screen != Screen::InGame || !app.cam.hitSphere(x, y, hit)) {
        ShowWindow(tip, SW_HIDE);
        return;
    }
    std::string txt = inspect::describePoint(app.world, app.cam, app.octaves, hit);
    SetWindowTextA(tip, txt.c_str());
    int wdt = 12 + (int)txt.size() * 9;
    // Keep clear of the news feed: the tooltip follows the cursor, and the
    // feed is a window above it, so an unclamped label hides two lines of news.
    int right = app.cam.width - (app.news && IsWindowVisible(app.news) ? newsWidth() : 0) - 4;
    int tx = std::min(x + 18, right - wdt), ty = y + 22;
    if (ty + 26 > app.cam.height) ty = y - 30;
    SetWindowPos(tip, HWND_TOP, tx, ty, wdt, 26, SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

// ------------------------------------------------ selection detail panels
// Custom-painted tabbed windows. Layout constants shared by painting and
// hit-testing; tab hit slots are fixed x-ranges so no text measuring is
// needed to route a click.

constexpr int PANEL_W = 470, PANEL_H = 330, PANEL_BAND_H = 260;
constexpr int PANEL_PAD = 12, PANEL_TAB_Y = 36, PANEL_TAB_H = 24, PANEL_CONTENT_Y = 70,
              PANEL_LINE_H = 21;
// Slot starts, plus the right edge as a final entry: painting draws each
// label at its slot and hit-testing takes the span up to the next, so the
// two cannot disagree.
static const int PANEL_TAB_X[6] = {12, 74, 176, 270, 356, 420};
static const char* PANEL_TABS[5] = {"People", "Environment", "Technology", "Buildings",
                                    "History"};
constexpr int PANEL_NTABS = 5;
enum : int { TAB_PEOPLE = 0, TAB_ENV, TAB_TECH, TAB_BUILT, TAB_HISTORY };
static std::string panelContent(const Panel& pn) {
    if (pn.kind == 1) return inspect::bandText(app.world, pn.bandId);
    int idx = inspect::settlementIndexById(app.world.pop, pn.sid);
    if (idx < 0) return "This settlement is gone:\nthey picked up and moved on.";
    const population::Settlement& st = app.world.pop.settlements[idx];
    if (pn.tab == TAB_PEOPLE) return inspect::peopleText(app.world, st);
    if (pn.tab == TAB_ENV) return inspect::envText(app.world, st);
    if (pn.tab == TAB_BUILT) return inspect::buildingsText(st, app.world.simTime);
    if (pn.tab == TAB_HISTORY) return inspect::historyText(app.world, st);
    if (pn.techSel >= 0) return inspect::techDetailText(app.world, st, idx, pn.techSel);
    std::string out;
    for (int t = 0; t < population::NTECH; t++)
        out += inspect::techStateLine(st.tech[t], t, app.world.simTime) + "\n";
    out += "\n(click a technology for details)";
    return out;
}

static Panel* panelFor(HWND h) {
    for (Panel& p : app.panels)
        if (p.wnd == h) return &p;
    return nullptr;
}

// ------------------------------------------------ the news feed
// What happened while the clock was running, grouped by kind: click a
// group to see its entries, an entry to see the detail, and "Go to" to
// put the camera on whoever it happened to.

constexpr int NEWS_W = 320, NEWS_LINE = 20, NEWS_TOP = 40;
// Collapsed, the feed is a tab just wide enough for the chevron and a
// three-character count, and wide enough to be an easy click target.
constexpr int NEWS_TAB_W = 34;
// The chevron sits in the top-right of the open panel. Its hit box must
// stay clear of the title, which is the way back up a level -- these two
// x-ranges are the invariant: [12, NEWS_W-30) is the title, and
// [NEWS_W-26, NEWS_W-6) is the chevron.
constexpr int NEWS_CHEVRON_X = NEWS_W - 26, NEWS_CHEVRON_R = NEWS_W - 6;

inline int newsWidth() { return app.newsOpen ? NEWS_W : NEWS_TAB_W; }

static void layoutNews() {
    if (!app.news) return;
    int w = newsWidth();
    int h = app.newsOpen ? app.cam.height - 76 : NEWS_TAB_W; // shut: a small square
    SetWindowPos(app.news, nullptr, app.cam.width - w, 56, w, h, SWP_NOZORDER);
    InvalidateRect(app.news, nullptr, TRUE);
}
static const char* NEWS_LABEL[population::EV_KINDS][2] = {
    {"tribe abandoned its home", "tribes abandoned their homes"},
    {"band of colonists set out", "bands of colonists set out"},
    {"tribe settled again", "tribes settled again"},
    {"new settlement founded", "new settlements founded"},
    {"band gave up and joined another", "bands gave up and joined others"},
    {"band perished on the road", "bands perished on the road"},
    {"raid was launched", "raids were launched"},
    {"settlement was raided", "settlements were raided"},
    {"raid was beaten off", "raids were beaten off"},
    {"raiding party came home", "raiding parties came home"},
    {"technology was invented!", "technologies were invented!"},
    {"settlement took up a technology", "settlements took up technologies"},
    {"granary was built", "granaries were built"},
    {"regional herd was hunted out", "regional herds were hunted out"},
    {"people lost a technology", "peoples lost technologies"},
    {"farmstead was raised", "farmsteads were raised"},
};

// The entries of one kind, in order.
static std::vector<const population::Event*> newsEntries(int kind) {
    std::vector<const population::Event*> v;
    for (const population::Event& e : app.world.pop.events)
        if (e.kind == kind) v.push_back(&e);
    return v;
}

static void openPanel(int kind, uint32_t sid, uint32_t bandId, int tab = -1); // defined below

// Take the camera to whoever an event happened to, and open their panel:
// at any useful zoom several settlements sit within a few pixels of each
// other, so moving the view alone leaves it to guesswork which one the
// news was about.
static void goToEvent(const population::Event& e) {
    // Follow the band if it is still out there; otherwise the settlement
    // it belongs to, which is where its people ended up.
    terrain::V3 at{};
    bool found = false;
    if (e.bandId) {
        for (const population::Band& b : app.world.pop.bands)
            if (b.id == e.bandId) { at = {b.px, b.py, b.pz}; found = true; }
    }
    if (!found) {
        int i = inspect::settlementIndexById(app.world.pop, e.sid);
        if (i < 0) i = inspect::settlementIndexById(app.world.pop, e.sid2);
        if (i >= 0) { at = sim::cellCentre(app.world.pop.settlements[i].cell); found = true; }
    }
    if (!found && e.cell >= 0) { // everyone involved is gone: go to the place
        at = sim::cellCentre(e.cell);
        found = true;
    }
    // Open the ones who are still there, so there is no doubt who is meant.
    bool bandAlive = false;
    for (const population::Band& b : app.world.pop.bands)
        if (b.id == e.bandId) bandAlive = true;
    if (bandAlive) openPanel(1, 0, e.bandId);
    else if (inspect::settlementIndexById(app.world.pop, e.sid) >= 0)
        openPanel(0, e.sid, 0);
    else if (inspect::settlementIndexById(app.world.pop, e.sid2) >= 0)
        openPanel(0, e.sid2, 0);
    if (!found) return;
    app.cam.lat = std::asin(std::clamp(at.z, -1.0f, 1.0f));
    app.cam.lon = std::atan2(at.y, at.x);
    if (app.cam.altitude > 900.0 / camera::EARTH_RADIUS_KM)
        app.cam.altitude = 900.0 / camera::EARTH_RADIUS_KM;
    app.cam.clampAltitude();
}

static void paintNews(HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    FillRect(dc, &rc, app.bgBrush);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, app.panelBold);
    if (!app.newsOpen) {
        // Shut, the feed is a small square with the way back in. Whether
        // there is news at all shows as the colour of the chevron.
        int total = 0;
        for (int k = 0; k < population::EV_KINDS; k++) total += app.world.pop.eventCount[k];
        SetTextColor(dc, total ? RGB(255, 215, 130) : RGB(140, 140, 155));
        TextOutA(dc, 12, 7, "<", 1);
        EndPaint(h, &ps);
        return;
    }
    SetTextColor(dc, RGB(235, 235, 240));
    const char* title = app.newsLevel == 0 ? "What happened" : "< back";
    TextOutA(dc, 12, 10, title, (int)strlen(title));
    SetTextColor(dc, RGB(255, 215, 130));
    TextOutA(dc, NEWS_CHEVRON_X, 10, ">", 1);
    SelectObject(dc, app.panelFont);
    int y = NEWS_TOP;
    int maxLines = (rc.bottom - NEWS_TOP) / NEWS_LINE - 1;
    if (app.newsLevel == 0) {
        bool any = false;
        for (int k = 0; k < population::EV_KINDS; k++) {
            int n = app.world.pop.eventCount[k];
            if (!n) continue;
            any = true;
            char line[128];
            snprintf(line, sizeof line, "%d %s", n, NEWS_LABEL[k][n == 1 ? 0 : 1]);
            SetTextColor(dc, RGB(255, 215, 130));
            TextOutA(dc, 12, y, line, (int)strlen(line));
            y += NEWS_LINE;
        }
        if (!any) {
            SetTextColor(dc, RGB(140, 140, 155));
            const char* q = "Nothing of note.";
            TextOutA(dc, 12, y, q, (int)strlen(q));
        }
    } else if (app.newsLevel == 1) {
        std::vector<const population::Event*> v = newsEntries(app.newsKind);
        for (int i = app.newsScroll; i < (int)v.size() && y < rc.bottom - NEWS_LINE; i++) {
            SetTextColor(dc, RGB(255, 215, 130));
            TextOutA(dc, 12, y, v[i]->text, (int)strlen(v[i]->text));
            y += NEWS_LINE;
        }
        int shown = (int)v.size() - app.newsScroll;
        if (shown > maxLines || app.world.pop.eventCount[app.newsKind] > (int)v.size()) {
            SetTextColor(dc, RGB(140, 140, 155));
            char more[96];
            snprintf(more, sizeof more, "(%d of %d; scroll with the wheel)",
                     std::min(shown, maxLines), app.world.pop.eventCount[app.newsKind]);
            TextOutA(dc, 12, rc.bottom - NEWS_LINE, more, (int)strlen(more));
        }
    } else {
        std::vector<const population::Event*> v = newsEntries(app.newsKind);
        if (app.newsPick < (int)v.size()) {
            const population::Event& e = *v[app.newsPick];
            char line[160];
            int yr = (int)(e.t / 365.0) + 1;
            snprintf(line, sizeof line, "Year %d", yr);
            SetTextColor(dc, RGB(230, 230, 235));
            TextOutA(dc, 12, y, line, (int)strlen(line));
            y += NEWS_LINE;
            // The text is already the human account; wrap it over two lines.
            std::string t = e.text;
            size_t cut = t.size() > 40 ? t.rfind(' ', 40) : std::string::npos;
            if (cut == std::string::npos) cut = t.size();
            std::string l1 = t.substr(0, cut), l2 = cut < t.size() ? t.substr(cut + 1) : "";
            TextOutA(dc, 12, y, l1.c_str(), (int)l1.size());
            y += NEWS_LINE;
            if (l2.size()) { TextOutA(dc, 12, y, l2.c_str(), (int)l2.size()); y += NEWS_LINE; }
            int si = inspect::settlementIndexById(app.world.pop, e.sid),
                s2 = inspect::settlementIndexById(app.world.pop, e.sid2);
            if (si >= 0) {
                snprintf(line, sizeof line, "Who: %s", app.world.pop.settlements[si].name);
                TextOutA(dc, 12, y, line, (int)strlen(line));
                y += NEWS_LINE;
            }
            if (s2 >= 0) {
                snprintf(line, sizeof line, "Other party: %s", app.world.pop.settlements[s2].name);
                TextOutA(dc, 12, y, line, (int)strlen(line));
                y += NEWS_LINE;
            }
            if (e.amount > 0) {
                snprintf(line, sizeof line, "How many: %d", (int)e.amount);
                TextOutA(dc, 12, y, line, (int)strlen(line));
                y += NEWS_LINE;
            }
            if (e.lossHere > 0 || e.lossThem > 0) {
                snprintf(line, sizeof line, "Dead: %d here, %d attacking",
                         (int)std::lround(e.lossHere), (int)std::lround(e.lossThem));
                TextOutA(dc, 12, y, line, (int)strlen(line));
                y += NEWS_LINE;
            }
            if (e.bandId) { // only events where somebody set out have a band
                bool alive = false;
                for (const population::Band& b : app.world.pop.bands)
                    if (b.id == e.bandId) alive = true;
                snprintf(line, sizeof line, "%s", alive ? "The band is still out there."
                                                        : "They are back among their people.");
                SetTextColor(dc, RGB(140, 140, 155));
                TextOutA(dc, 12, y, line, (int)strlen(line));
                y += NEWS_LINE;
            }
            y += 6;
            SetTextColor(dc, RGB(255, 215, 130));
            const char* go = "[ Go to ]";
            TextOutA(dc, 12, y, go, (int)strlen(go));
        }
    }
    EndPaint(h, &ps);
}

static LRESULT CALLBACK newsProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:
        paintNews(h);
        return 0;
    case WM_MOUSEWHEEL:
        if (app.newsLevel == 1) {
            app.newsScroll = std::max(0, app.newsScroll - GET_WHEEL_DELTA_WPARAM(wp) / 120);
            InvalidateRect(h, nullptr, TRUE);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
        if (!app.newsOpen) { // the whole tab opens it again
            app.newsOpen = true;
            layoutNews();
            return 0;
        }
        if (my < NEWS_TOP && mx >= NEWS_CHEVRON_X && mx < NEWS_CHEVRON_R) {
            app.newsOpen = false; // out of the way, without losing the news
            layoutNews();
            return 0;
        }
        if (my < NEWS_TOP) { // the title doubles as the way back
            if (app.newsLevel > 0) app.newsLevel--;
            app.newsScroll = 0;
            InvalidateRect(h, nullptr, TRUE);
            return 0;
        }
        int row = (my - NEWS_TOP) / NEWS_LINE;
        if (app.newsLevel == 0) {
            int seen = 0;
            for (int k = 0; k < population::EV_KINDS; k++) {
                if (!app.world.pop.eventCount[k]) continue;
                if (seen == row) {
                    app.newsKind = k;
                    app.newsLevel = 1;
                    app.newsScroll = 0;
                    break;
                }
                seen++;
            }
        } else if (app.newsLevel == 1) {
            std::vector<const population::Event*> v = newsEntries(app.newsKind);
            int idx = app.newsScroll + row;
            if (idx < (int)v.size()) {
                app.newsPick = idx;
                app.newsLevel = 2;
            }
        } else {
            std::vector<const population::Event*> v = newsEntries(app.newsKind);
            if (app.newsPick < (int)v.size()) goToEvent(*v[app.newsPick]);
        }
        InvalidateRect(h, nullptr, TRUE);
        return 0;
    }
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static void refreshNews() {
    if (!app.news) return;
    app.newsLevel = 0;
    app.newsScroll = 0;
    ShowWindow(app.news, app.screen == Screen::InGame ? SW_SHOW : SW_HIDE);
    InvalidateRect(app.news, nullptr, TRUE);
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
        if (pn->kind == 0) {
            int si = inspect::settlementIndexById(app.world.pop, pn->sid);
            const char* nm = si >= 0 ? app.world.pop.settlements[si].name : "";
            snprintf(title, sizeof title, "%s", nm[0] ? nm : "Settlement");
        } else {
            const char* nm = "";
            const char* what = "Band";
            for (const population::Band& bd : app.world.pop.bands)
                if (bd.id == pn->bandId) {
                    nm = bd.name;
                    what = bd.purpose == population::BAND_RAID ? "raiders"
                           : bd.colonists                      ? "colonists"
                                                               : "on the move";
                }
            if (nm[0]) snprintf(title, sizeof title, "%s %s", nm, what);
            else snprintf(title, sizeof title, "Band %u", pn->bandId);
        }
        SelectObject(dc, app.panelBold);
        SetTextColor(dc, RGB(235, 235, 240));
        TextOutA(dc, PANEL_PAD, 8, title, (int)strlen(title));
        SelectObject(dc, app.panelFont);
        if (pn->kind == 0)
            for (int t = 0; t < PANEL_NTABS; t++) {
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
            bool clickable = pn->kind == 0 && pn->tab == TAB_TECH &&
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
            for (int t = 0; t < PANEL_NTABS; t++)
                if (mx >= PANEL_TAB_X[t] && mx < PANEL_TAB_X[t + 1] - 6) {
                    pn->tab = t;
                    pn->techSel = -1;
                    InvalidateRect(h, nullptr, TRUE);
                    return 0;
                }
        }
        if (pn && pn->kind == 0 && pn->tab == TAB_TECH && my >= PANEL_CONTENT_Y) {
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

static void openPanel(int kind, uint32_t sid, uint32_t bandId, int tab) {
    for (Panel& pn : app.panels)
        if (pn.kind == kind && pn.sid == sid && pn.bandId == bandId) {
            if (tab >= 0 && pn.tab != tab) {
                pn.tab = tab;
                pn.techSel = -1;
                InvalidateRect(pn.wnd, nullptr, TRUE);
            }
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
    app.panels.push_back({w, kind, sid, bandId, tab > 0 ? tab : 0});
    SetWindowPos(w, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

// A click on the globe: open a detail panel for the settlement or band whose
// marker is under the cursor (same radii the shader draws them with).
static void pickAt(int x, int y) {
    if (app.screen != Screen::InGame) return;
    // An event mark is hit before anything under it: it sits above the
    // marker on purpose, and it is the smaller target. It opens the history
    // of whoever it happened to, which is the whole sentence the news feed
    // would have given.
    for (const App::MarkHit& m : app.markHits)
        if (x >= m.x - 1 && x < m.x + m.w + 1 && y >= m.y - 1 && y < m.y + m.h + 1) {
            if (m.sid) openPanel(0, m.sid, 0, TAB_HISTORY);
            else if (m.bandId) openPanel(1, 0, m.bandId);
            return;
        }
    camera::Vec3 hit;
    if (!app.cam.hitSphere(x, y, hit)) return;
    terrain::V3 n = {(float)hit.x, (float)hit.y, (float)hit.z};
    const population::Field& pf = app.world.pop;
    // Pick radius = draw radius plus ~3 px of slop: clicks are aim-limited.
    // Close up that is the village itself; further out it is the marker, so
    // it stays the same size on screen however far the view is zoomed.
    double kmpp = app.cam.kmPerPixel();
    float sRadius = kmpp < HUT_KMPP ? 0.15f : (float)(kmpp * (markerRadius(kmpp) + 3));
    int best = -1;
    float bestD = sRadius;
    for (int i = 0; i < (int)pf.settlements.size(); i++) {
        float d = sim::distKm(n, sim::cellCentre(pf.settlements[i].cell));
        if (d < bestD) { bestD = d; best = i; }
    }
    if (best >= 0) { openPanel(0, pf.settlements[best].id, 0); return; }
    float bRadius = kmpp < WALK_KMPP ? 0.08f : (float)(kmpp * (markerRadius(kmpp) + 3));
    uint32_t bestId = 0;
    bestD = bRadius;
    for (const population::Band& bd : pf.bands) {
        float d = sim::distKm(n, {bd.px, bd.py, bd.pz});
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
    app.world.pop.events.clear();
    for (int k = 0; k < population::EV_KINDS; k++) app.world.pop.eventCount[k] = 0;
    app.world.simTime += days;
    updateDateLabel();
    population::Field& pf = app.world.pop;
    if (pf.settlements.empty()) return;
    bool any = sim::simulate(pf, app.world.tech, app.world.hydro, app.world.clim, app.world.simTime);
    if (any && app.tex.popTex) textures::uploadPopulation(app.tex, app.world.pop);
    refreshPanels();
    refreshNews();
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
        if (app.news)
            layoutNews();
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
        app.drag.lastX = GET_X_LPARAM(lp);
        app.drag.lastY = GET_Y_LPARAM(lp);
        app.downX = app.drag.lastX;
        app.downY = app.drag.lastY;
        app.clickMoved = false;
        app.drag.anchorValid = app.cam.hitSphere(app.drag.lastX, app.drag.lastY, app.drag.anchor);
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
            camera::applyDrag(app.cam, app.drag, x, y);
            app.drag.lastX = x;
            app.drag.lastY = y;
        }
        updateTooltip(x, y);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (app.screen != Screen::InGame) return 0;
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        double factor = std::pow(0.8, delta / (double)WHEEL_DELTA);
        POINT cur{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; // screen coordinates
        ScreenToClient(hwnd, &cur);
        camera::zoomAt(app.cam, factor, (int)cur.x, (int)cur.y);
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

    WNDCLASSA nc = {};
    nc.lpfnWndProc = newsProc;
    nc.hInstance = inst;
    nc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    nc.hbrBackground = CreateSolidBrush(RGB(8, 8, 16));
    nc.lpszClassName = "IBNews";
    RegisterClassA(&nc);

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
    if (!gl::loadGL()) return 1;
    if (wglSwapIntervalEXT) wglSwapIntervalEXT(1);

    app.program = gl::buildProgram(world::exeDir() + "\\shaders\\");
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
    glUniform1i(glGetUniformLocation(app.program, "uBands"), 5);
    glUniform1i(glGetUniformLocation(app.program, "uSites"), 6);
    glUniform1i(glGetUniformLocation(app.program, "uOverlay"), 7);
    glUniform1i(glGetUniformLocation(app.program, "uEarth"), 8);
    GLint uUseEarth = glGetUniformLocation(app.program, "uUseEarth");
    GLint uDoy = glGetUniformLocation(app.program, "uDoy");
    GLint uClock = glGetUniformLocation(app.program, "uClock");
    GLint uAware = glGetUniformLocation(app.program, "uAware");
    GLint uAwareCount = glGetUniformLocation(app.program, "uAwareCount");

    createControls();
    app.news = CreateWindowA("IBNews", "", WS_CHILD | WS_BORDER | WS_CLIPSIBLINGS,
                             app.cam.width - NEWS_W, 56, NEWS_W, app.cam.height - 76, app.hwnd,
                             nullptr, inst, nullptr);
    app.cam.clampAltitude();

    // Testing shortcut:
    // humanhistory <latDeg> <lonDeg> [altitudeKm] [seed] [land%] [conc%] [debugmode] [fastForwardYears]
    // A seed of "@name" loads worlds\name.ibw instead of generating (testing).
    if (argc >= 3) {
        bool loaded = argc >= 5 && argv[4][0] == '@' &&
                      savefile::load(argv[4] + 1, app.world, app.cam, buildProgress);
        if (!loaded) {
            app.world.earth = argc >= 5 && _stricmp(argv[4], "earth") == 0;
            app.world.seed = app.world.earth ? 1u : (argc >= 5 ? (uint32_t)strtoul(argv[4], nullptr, 10) : 0);
            if (argc >= 6) app.world.landPercent = (float)atof(argv[5]);
            if (argc >= 7) app.world.concentration = (float)atof(argv[6]);
            app.world.build(buildProgress);
        }
        if (argc >= 8) {
            std::string d = argv[7];
            app.debugMode = d == "plates" ? 1 : d == "substrate" ? 2 : d == "vegetation" ? 3
                          : d == "population" ? 4 : d == "climate" ? 5 : 0;
        }
        textures::uploadAll(app.tex, app.world);
        app.cam.lat = atof(argv[1]) * camera::PI / 180;
        app.cam.lon = atof(argv[2]) * camera::PI / 180;
        if (argc >= 4) app.cam.altitude = atof(argv[3]) / camera::EARTH_RADIUS_KM;
        app.cam.clampAltitude();
        setScreen(Screen::InGame);
        if (argc >= 9) {
            advanceDays(atof(argv[8]) * 365.0); // fast-forward years
            int gran = 0, building = 0, fstead = 0;
            for (const population::Settlement& s : app.world.pop.settlements) {
                gran += (int)(s.granaries + 0.5f);
                building += s.buildWork > 0 ? 1 : 0;
                fstead += (int)(s.farmsteads + 0.5f);
            }
            double totalP = 0;
            population::Cohorts all{};
            for (const population::Settlement& s : app.world.pop.settlements) {
                totalP += s.P;
                all.add(s.pop);
            }
            for (int i = 0; i < 6 && i < (int)app.world.pop.settlements.size(); i++) {
                const population::Settlement& sx =
                    app.world.pop.settlements[i * app.world.pop.settlements.size() / 6];
                fprintf(stderr, "  %s of the %s\n", sx.name,
                        sx.culture < app.world.pop.cultures.size()
                            ? app.world.pop.cultures[sx.culture].name
                            : "?");
            }
            double tp = std::max(totalP, 1.0);
            fprintf(stderr,
                    "people: %.0f%% children, %.0f%% men, %.0f%% women, %.0f%% elderly\n",
                    all.C / tp * 100, all.M / tp * 100, all.W / tp * 100, all.E / tp * 100);
            fprintf(stderr,
                    "granaries built: %d, under construction: %d, farmsteads: %d\n"
                    "settlements: %d, people: %.0f, bands: %d (peak %d), ruins: %d, "
                    "worked sites: %d\n",
                    gran, building, fstead, (int)app.world.pop.settlements.size(), totalP,
                    (int)app.world.pop.bands.size(), (int)app.world.pop.peakBands,
                    (int)app.world.pop.ruins.size(), (int)app.world.pop.scars.size());
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
            camera::Camera& c = app.cam;
            camera::Vec3 p = c.position(), f = c.forward(), rt = c.right(), u = c.up();
            // Finest noise octave should be around two pixels wide on screen.
            double kmpp = c.kmPerPixel();
            int octaves = (int)std::ceil(std::log2(camera::EARTH_RADIUS_KM / (2.0 * kmpp)));
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
                    int sIdx =
                        pn.kind == 0 ? inspect::settlementIndexById(app.world.pop, pn.sid) : -1;
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
            glUniform1i(uUseEarth, app.world.earth && terrain::TEMPLATE.active ? 1 : 0);
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
            // Markers and names: redrawn every frame, since they follow the
            // camera as much as the world.
            if (overlayStale()) {
                paintOverlay();
                uploadOverlay();
            }
            glBindTexture(GL_TEXTURE_2D, app.tex.hydroTex);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            // Self-screenshot from the back buffer: defined even when the
            // window is occluded, unlike PrintWindow. Testing tooling.
            if (!app.shotPath.empty()) {
                int W = app.cam.width, H = app.cam.height;
                std::vector<unsigned char> px(W * H * 3);
                glReadPixels(0, 0, W, H, 0x80E0 /*GL_BGR*/, GL_UNSIGNED_BYTE, px.data());
                if (!bmp::write(app.shotPath, W, H, px.data()))
                    fprintf(stderr, "shot: could not write %s\n", app.shotPath.c_str());
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
