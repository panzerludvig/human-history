// The marker overlay: settlements, bands, their names and the event marks
// above them, drawn with GDI into a screen-sized image every time the camera,
// the world or the window changes, and laid over the globe by the shader,
// which keys on magenta for "nothing here". Technical/Globe Viewer.md
// §Population describes the markers and their thinning; the design of the
// event marks is the comment at MarkGlyph below.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>
#include "gl.h"
#include "camera.h"
#include "world.h"
#include "sim.h"

namespace overlay {

// Where an event chip was drawn, so a click can find it. A chip belongs to
// a settlement or to a band, never both.
struct MarkHit {
    int x, y, w, h;
    uint32_t sid, bandId;
};

// A marker candidate: where it lands on screen, its weight for the
// thinning, and which settlement or band it is.
struct Cand {
    float x, y, w;
    int idx;
    bool band;
};

// The image, its texture, the fonts and brushes, the scratch paint works
// in, and what the last frame was drawn for.
struct State {
    HDC ovDC = nullptr;
    HBITMAP ovBmp = nullptr;
    unsigned char* ovBits = nullptr;
    int ovW = 0, ovH = 0;
    GLuint ovTex = 0;
    HFONT markerFont = nullptr, markerBold = nullptr, markBold = nullptr;
    std::vector<MarkHit> markHits; // rebuilt with the overlay
    // Scratch for paint, kept between redraws so a camera-move frame does
    // not allocate (standards/cpp.md §Hot paths): the candidates in view
    // and the marks per settlement and band. The winner-per-square map is
    // not kept: its iteration order is the draw order, and a reused table
    // would make that depend on what was drawn before.
    std::vector<Cand> cands;
    std::unordered_map<uint32_t, std::vector<int>> markSite, markBand;
    // The brushes and pen, created once with the fonts.
    HBRUSH clearBr = nullptr, cream = nullptr, amber = nullptr, ink = nullptr;
    HBRUSH edgeFill = nullptr, countBr = nullptr, family[5] = {};
    HPEN edge = nullptr;
    // The overlay is a function of the camera, the world and the window, so
    // it only needs redrawing when one of those moves. Repainting and
    // uploading it costs about 2.4 ms, which is a quarter of a frame to spend
    // on a picture that usually has not changed.
    double lastLat = 1e9, lastLon = 1e9, lastAlt = 0, lastT = -1;
    int lastW = 0, lastH = 0, lastSettlements = -1, lastBands = -1, lastScreen = -1;
};

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
inline int markerRadius(double kmpp) {
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
struct MarkPoly {
    const float* xy;
    int n;
};
struct MarkGlyph {
    const MarkPoly* fill;
    int nf;
    const MarkPoly* cut;
    int nc;
};

// clang-format off
constexpr float MP_RELOCATE[] = {1,4, 6,4, 6,2, 10,5.5f, 6,9, 6,7, 1,7};
constexpr float MP_SPLIT_A[]  = {0.4f,4.7f, 4.6f,4.7f, 4.6f,6.3f, 0.4f,6.3f};
constexpr float MP_SPLIT_B[]  = {3.5f,4.9f, 4.6f,6.1f, 8.2f,2.9f, 7.1f,1.7f};
constexpr float MP_SPLIT_C[]  = {6.3f,1.5f, 10.2f,0.6f, 9.2f,4.4f};
constexpr float MP_SPLIT_D[]  = {3.5f,6.1f, 4.6f,4.9f, 8.2f,8.1f, 7.1f,9.3f};
constexpr float MP_SPLIT_E[]  = {6.3f,9.5f, 10.2f,10.4f, 9.2f,6.6f};
constexpr float MP_SET_A[]    = {2,9, 9,9, 9,10, 2,10};
constexpr float MP_SET_B[]    = {5.5f,1, 10,5, 1,5};
constexpr float MP_SET_C[]    = {2,5, 9,5, 9,8, 2,8};
constexpr float MP_FOUND_A[]  = {5.5f,2, 10,6, 1,6};
constexpr float MP_FOUND_B[]  = {2,6, 9,6, 9,10, 2,10};
constexpr float MP_FOUND_C[]  = {0,1, 2,0, 3,2, 1,3};
constexpr float MP_MERGE_A[]  = {1.0f,1.4f, 2.1f,0.4f, 5.6f,4.1f, 4.5f,5.1f};
constexpr float MP_MERGE_B[]  = {1.0f,9.6f, 2.1f,10.6f, 5.6f,6.9f, 4.5f,5.9f};
constexpr float MP_MERGE_C[]  = {4.2f,4.7f, 8.0f,4.7f, 8.0f,6.3f, 4.2f,6.3f};
constexpr float MP_MERGE_D[]  = {7.4f,2.6f, 10.8f,5.5f, 7.4f,8.4f};
constexpr float MP_DIED_A[]   = {4,1, 7,1, 7,10, 4,10};
constexpr float MP_DIED_B[]   = {1,3, 10,3, 10,5, 1,5};
constexpr float MP_BLADE[]    = {2.1f,9.6f, 3.3f,8.4f, 10.4f,1.3f, 10.9f,0.2f, 9.6f,0.6f, 2.6f,7.7f};
constexpr float MP_BLADE2[]   = {8.9f,9.6f, 7.7f,8.4f, 0.6f,1.3f, 0.1f,0.2f, 1.4f,0.6f, 8.4f,7.7f};
constexpr float MP_GUARD[]    = {1.0f,7.3f, 1.9f,6.4f, 4.6f,9.1f, 3.7f,10.0f};
constexpr float MP_GUARD2[]   = {10.0f,7.3f, 9.1f,6.4f, 6.4f,9.1f, 7.3f,10.0f};
constexpr float MP_POMMEL[]   = {0.4f,9.0f, 1.6f,7.8f, 3.2f,9.4f, 2.0f,10.6f};
constexpr float MP_SHIELD[]   = {1,1, 10,1, 10,5, 5.5f,10, 1,5};
constexpr float MP_HOME[]     = {10,4, 5,4, 5,2, 1,5.5f, 5,9, 5,7, 10,7};
constexpr float MP_STAR[]     = {5.5f,0, 7,4, 11,5.5f, 7,7, 5.5f,11, 4,7, 0,5.5f, 4,4};
constexpr float MP_STAR_S[]   = {5.5f,0, 6.6f,3, 9.5f,4, 6.6f,5, 5.5f,8, 4.4f,5, 1.5f,4, 4.4f,3};
constexpr float MP_DOWN[]     = {4.5f,7, 6.5f,7, 6.5f,9, 8,9, 5.5f,11, 3,9, 4.5f,9};
constexpr float MP_GRAN_A[]   = {2,1, 9,1, 10,4, 1,4};
constexpr float MP_GRAN_B[]   = {2,4, 9,4, 9,8, 2,8};
constexpr float MP_GRAN_C[]   = {2,8, 3.5f,8, 3.5f,11, 2,11};
constexpr float MP_GRAN_D[]   = {7.5f,8, 9,8, 9,11, 7.5f,11};
constexpr float MP_BONE_A[]   = {0.6f,10.4f, 1.6f,11.2f, 10.4f,6.6f, 9.4f,5.8f};
constexpr float MP_BONE_B[]   = {0.6f,6.6f, 1.6f,5.8f, 10.4f,10.4f, 9.4f,11.2f};
constexpr float MP_KNOB_A[]   = {0.0f,10.0f, 1.4f,9.3f, 2.0f,10.6f, 0.6f,11.3f};
constexpr float MP_KNOB_B[]   = {9.0f,5.7f, 10.4f,5.0f, 11.0f,6.3f, 9.6f,7.0f};
constexpr float MP_KNOB_C[]   = {0.0f,6.3f, 1.4f,7.0f, 2.0f,5.7f, 0.6f,5.0f};
constexpr float MP_KNOB_D[]   = {9.0f,10.6f, 10.4f,11.3f, 11.0f,10.0f, 9.6f,9.3f};
constexpr float MP_SKULL[]    = {2.6f,1.2f, 3.6f,0.2f, 7.4f,0.2f, 8.4f,1.2f, 8.4f,5.4f, 2.6f,5.4f};
constexpr float MP_JAW[]      = {3.7f,5.4f, 7.3f,5.4f, 7.3f,7.4f, 3.7f,7.4f};
constexpr float MP_SLASH[]    = {0.6f,9.4f, 2.0f,10.8f, 10.4f,2.4f, 9.0f,1.0f};
constexpr float MP_EYE_L[]    = {3.5f,2.0f, 5.0f,2.0f, 5.0f,4.0f, 3.5f,4.0f};
constexpr float MP_EYE_R[]    = {6.0f,2.0f, 7.5f,2.0f, 7.5f,4.0f, 6.0f,4.0f};
constexpr float MP_NOSE[]     = {5.0f,4.4f, 6.0f,4.4f, 6.0f,5.4f, 5.0f,5.4f};
constexpr float MP_TOOTH_L[]  = {4.6f,6.2f, 5.1f,6.2f, 5.1f,7.4f, 4.6f,7.4f};
constexpr float MP_TOOTH_R[]  = {5.9f,6.2f, 6.4f,6.2f, 6.4f,7.4f, 5.9f,7.4f};

#define MPOLY(a) {a, (int)(sizeof(a) / sizeof(float) / 2)}
constexpr MarkPoly MG_RELOCATE[] = {MPOLY(MP_RELOCATE)};
constexpr MarkPoly MG_SPLIT[]    = {MPOLY(MP_SPLIT_A), MPOLY(MP_SPLIT_B), MPOLY(MP_SPLIT_C),
                                       MPOLY(MP_SPLIT_D), MPOLY(MP_SPLIT_E)};
constexpr MarkPoly MG_SETTLED[]  = {MPOLY(MP_SET_A), MPOLY(MP_SET_B), MPOLY(MP_SET_C)};
constexpr MarkPoly MG_FOUNDED[]  = {MPOLY(MP_FOUND_A), MPOLY(MP_FOUND_B), MPOLY(MP_FOUND_C)};
constexpr MarkPoly MG_MERGED[]   = {MPOLY(MP_MERGE_A), MPOLY(MP_MERGE_B), MPOLY(MP_MERGE_C),
                                       MPOLY(MP_MERGE_D)};
constexpr MarkPoly MG_PERISHED[] = {MPOLY(MP_DIED_A), MPOLY(MP_DIED_B)};
constexpr MarkPoly MG_RAIDOUT[]  = {MPOLY(MP_BLADE), MPOLY(MP_GUARD), MPOLY(MP_POMMEL)};
constexpr MarkPoly MG_RAIDHIT[]  = {MPOLY(MP_BLADE), MPOLY(MP_BLADE2), MPOLY(MP_GUARD),
                                       MPOLY(MP_GUARD2)};
constexpr MarkPoly MG_RAIDHELD[] = {MPOLY(MP_SHIELD)};
constexpr MarkPoly MG_RAIDHOME[] = {MPOLY(MP_HOME)};
constexpr MarkPoly MG_INVENTED[] = {MPOLY(MP_STAR)};
constexpr MarkPoly MG_ADOPTED[]  = {MPOLY(MP_STAR_S), MPOLY(MP_DOWN)};
constexpr MarkPoly MG_GRANARY[]  = {MPOLY(MP_GRAN_A), MPOLY(MP_GRAN_B), MPOLY(MP_GRAN_C),
                                       MPOLY(MP_GRAN_D)};
constexpr MarkPoly MG_GAME[]     = {MPOLY(MP_BONE_A), MPOLY(MP_BONE_B), MPOLY(MP_KNOB_A),
                                       MPOLY(MP_KNOB_B), MPOLY(MP_KNOB_C), MPOLY(MP_KNOB_D),
                                       MPOLY(MP_SKULL), MPOLY(MP_JAW)};
constexpr MarkPoly MG_GAME_CUT[] = {MPOLY(MP_EYE_L), MPOLY(MP_EYE_R), MPOLY(MP_NOSE),
                                       MPOLY(MP_TOOTH_L), MPOLY(MP_TOOTH_R)};
constexpr MarkPoly MG_LOST[]     = {MPOLY(MP_STAR)};
constexpr MarkPoly MG_LOST_CUT[] = {MPOLY(MP_SLASH)};
#undef MPOLY

#define MGLYPH(a) {a, (int)(sizeof(a) / sizeof(MarkPoly)), nullptr, 0}
// Indexed by population::EV_*, in that order.
constexpr MarkGlyph MARK_GLYPH[population::EV_KINDS] = {
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
inline int markFamily(int kind) {
    switch (kind) {
    case population::EV_RAID_LAUNCH:
    case population::EV_RAID_HIT:
    case population::EV_RAID_HELD:
    case population::EV_RAID_HOME:
        return 0; // violence
    case population::EV_GAME_GONE:
        return 1; // the land
    case population::EV_INVENTED:
    case population::EV_ADOPTED:
    case population::EV_TECH_LOST:
        return 3; // knowledge
    case population::EV_GRANARY:
        return 4; // building
    default:
        return 2; // movement
    }
}
inline COLORREF markColour(int family) {
    static const COLORREF c[5] = {RGB(163, 53, 42), RGB(107, 122, 74), RGB(232, 176, 66),
                                  RGB(216, 194, 90), RGB(185, 146, 90)};
    return c[family < 0 || family > 4 ? 2 : family];
}

// The fonts, brushes and pen, created once. Marker text is drawn without
// antialiasing on purpose: the overlay has no alpha channel -- the shader
// keys on magenta -- so blended edges would fringe. Crisp small type also
// suits a map.
inline void init(State& ov) {
    ov.markerFont = CreateFontA(15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                NONANTIALIASED_QUALITY, 0, "Segoe UI");
    ov.markerBold = CreateFontA(14, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                NONANTIALIASED_QUALITY, 0, "Segoe UI");
    ov.markBold = CreateFontA(11, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                              NONANTIALIASED_QUALITY, 0, "Segoe UI"); // the count in a chip
    ov.clearBr = CreateSolidBrush(RGB(255, 0, 255)); // "nothing here", to the shader
    ov.cream = CreateSolidBrush(RGB(238, 230, 208));
    ov.amber = CreateSolidBrush(RGB(232, 176, 66));
    ov.ink = CreateSolidBrush(RGB(96, 52, 28));
    ov.edge = CreatePen(PS_SOLID, 1, RGB(40, 28, 18));
    ov.edgeFill = CreateSolidBrush(RGB(40, 28, 18)); // the chip's dark rim
    ov.countBr = CreateSolidBrush(RGB(74, 64, 52));
    for (int f = 0; f < 5; f++) ov.family[f] = CreateSolidBrush(markColour(f));
}

constexpr int CHIP = 11;      // a mark, square
constexpr int CHIP_GAP = 1;   // and the air between two of them
constexpr int CHIP_SHOWN = 4; // before the rest become a number

// The screen-sized image, remade when the window changes size.
inline void ensure(State& ov, int width, int height) {
    if (ov.ovDC && ov.ovW == width && ov.ovH == height) return;
    if (ov.ovBmp) {
        DeleteObject(ov.ovBmp);
        ov.ovBmp = nullptr;
    }
    if (ov.ovDC) {
        DeleteDC(ov.ovDC);
        ov.ovDC = nullptr;
    }
    ov.ovW = width;
    ov.ovH = height;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = ov.ovW;
    bi.bmiHeader.biHeight = ov.ovH; // bottom-up, so rows arrive in OpenGL order
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    ov.ovDC = CreateCompatibleDC(screen);
    ov.ovBmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, (void**)&ov.ovBits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    SelectObject(ov.ovDC, ov.ovBmp);
    SetBkMode(ov.ovDC, TRANSPARENT);
}

// One mark: the family square, a dark edge, and the glyph cut out in cream.
inline void drawChip(HDC dc, const State& ov, int kind, int x, int y, HBRUSH edgeBr,
                     HBRUSH creamBr) {
    RECT r{x, y, x + CHIP, y + CHIP};
    FillRect(dc, &r, edgeBr);
    HBRUSH fam = ov.family[markFamily(kind)];
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
}

// The overflow: how many marks are not shown. The only chip that holds type.
inline void drawCountChip(HDC dc, const State& ov, int n, int x, int y, HBRUSH edgeBr) {
    RECT r{x, y, x + CHIP, y + CHIP};
    FillRect(dc, &r, edgeBr);
    RECT in{x + 1, y + 1, x + CHIP - 1, y + CHIP - 1};
    FillRect(dc, &in, ov.countBr);
    char t[8];
    snprintf(t, sizeof t, "%d", n > 99 ? 99 : n);
    SelectObject(dc, ov.markBold);
    UINT old = SetTextAlign(dc, TA_CENTER | TA_TOP);
    SetTextColor(dc, RGB(238, 230, 208));
    TextOutA(dc, x + CHIP / 2, y, t, (int)strlen(t));
    SetTextAlign(dc, old);
}

// A house seen from the side, sized to sit inside a marker of radius r.
inline void drawHutGlyph(HDC dc, int cx, int cy, int r) {
    int w = r * 5 / 9, hgt = r * 6 / 9;
    POINT roof[3] = {{cx - w - 1, cy - 1}, {cx, cy - hgt}, {cx + w + 1, cy - 1}};
    Polygon(dc, roof, 3);
    Rectangle(dc, cx - w + 2, cy - 1, cx + w - 1, cy + hgt - 1);
}

// One line of text centred on x, with a dark copy under it so a name stays
// legible over snow as well as over forest.
inline void drawLabel(HDC dc, int x, int y, const char* txt) {
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
inline void gatherMarks(const population::Field& pf,
                        std::unordered_map<uint32_t, std::vector<int>>& bySite,
                        std::unordered_map<uint32_t, std::vector<int>>& byBand) {
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
inline void drawMarkRow(HDC dc, State& ov, const std::vector<int>& kinds, int cx, int bottomY,
                        uint32_t sid, uint32_t bandId, HBRUSH edgeBr, HBRUSH creamBr) {
    if (kinds.empty()) return;
    int shown = (int)kinds.size() <= CHIP_SHOWN ? (int)kinds.size() : CHIP_SHOWN;
    int hidden = (int)kinds.size() - shown;
    int n = shown + (hidden > 0 ? 1 : 0);
    int w = n * CHIP + (n - 1) * CHIP_GAP;
    int x = cx - w / 2, y = bottomY - CHIP;
    for (int i = 0; i < shown; i++) {
        drawChip(dc, ov, kinds[i], x, y, edgeBr, creamBr);
        ov.markHits.push_back({x, y, CHIP, CHIP, sid, bandId});
        x += CHIP + CHIP_GAP;
    }
    if (hidden > 0) {
        drawCountChip(dc, ov, hidden, x, y, edgeBr);
        ov.markHits.push_back({x, y, CHIP, CHIP, sid, bandId});
    }
}

// Draw the markers, names and event marks for this frame into the image;
// inGame is false on the pause menu, where the globe shows bare.
inline void paint(State& ov, const world::World& w, const camera::Camera& cam, bool inGame) {
    ensure(ov, cam.width, cam.height);
    ov.markHits.clear();
    if (!ov.ovDC) return;
    HDC dc = ov.ovDC;
    RECT full{0, 0, ov.ovW, ov.ovH};
    FillRect(dc, &full, ov.clearBr);
    if (!inGame || w.pop.settlements.empty()) return;
    double kmpp = cam.kmPerPixel();
    // Close up the shader draws the houses and the walking people
    // themselves, so the overlay adds only the names.
    bool ground = kmpp < HUT_KMPP;   // houses are being drawn on the ground
    bool walking = kmpp < WALK_KMPP; // and the people of a band, one by one
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
    std::vector<Cand>& cands = ov.cands;
    cands.clear();
    std::unordered_map<long long, int> best; // fresh: its order is the draw order
    auto offer = [&](float x, float y, float w, int idx, bool band, const terrain::V3& at) {
        if (x < -60 || y < -30 || x > ov.ovW + 60 || y > ov.ovH + 30) return;
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
    const population::Field& pf = w.pop;
    float sx, sy;
    for (size_t i = 0; i < pf.settlements.size(); i++) {
        terrain::V3 at = sim::cellCentre(pf.settlements[i].cell);
        if (camera::projectToScreen(cam, {at.x, at.y, at.z}, sx, sy))
            offer(sx, sy, pf.settlements[i].P, (int)i, false, at);
    }
    // Bands outrank settlements for the square they stand on: they are the
    // thing that moves, and losing one to a village it is passing is worse
    // than losing the village.
    for (size_t i = 0; i < pf.bands.size(); i++) {
        const population::Band& b = pf.bands[i];
        terrain::V3 at{b.px, b.py, b.pz};
        if (camera::projectToScreen(cam, {at.x, at.y, at.z}, sx, sy))
            offer(sx, sy, b.P + 1e6f, (int)i, true, at);
    }

    std::unordered_map<uint32_t, std::vector<int>>& markSite = ov.markSite;
    std::unordered_map<uint32_t, std::vector<int>>& markBand = ov.markBand;
    markSite.clear();
    markBand.clear();
    gatherMarks(pf, markSite, markBand);
    HBRUSH cream = ov.cream, amber = ov.amber, ink = ov.ink, edgeFill = ov.edgeFill;
    HPEN edge = ov.edge;
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
                if (mr <= 2)
                    Rectangle(dc, cx - mr, cy - mr, cx + mr + 1, cy + mr + 1);
                else
                    Ellipse(dc, cx - mr, cy - mr, cx + mr + 1, cy + mr + 1);
                if (mr >= 6) { // the hut, wherever a band would still name itself
                    SelectObject(dc, ink);
                    drawHutGlyph(dc, cx, cy, mr);
                }
            }
            if (names) {
                SelectObject(dc, ov.markerFont);
                drawLabel(dc, cx, cy + below, st.name);
            }
            auto it = markSite.find(st.id);
            if (it != markSite.end() && !ground)
                drawMarkRow(dc, ov, it->second, cx, cy - mr - 4, st.id, 0, edgeFill, cream);
        } else {
            const population::Band& b = pf.bands[c.idx];
            const char* kind = b.purpose == population::BAND_RAID ? "Raiders"
                               : b.colonists                      ? "Colonists"
                                                                  : "Tribe";
            SelectObject(dc, ov.markerBold);
            SIZE ts{};
            GetTextExtentPoint32A(dc, kind, (int)strlen(kind), &ts);
            int hw = (int)ts.cx / 2 + 5, hh = 9;
            if (!names) {
                hw = std::max(mr + 2, 3);
                hh = std::max(mr, 2);
            } // no room for the word
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
                SelectObject(dc, ov.markerFont);
                drawLabel(dc, cx, cy + below, b.name);
            }
            auto it = markBand.find(b.id);
            if (it != markBand.end() && !walking)
                drawMarkRow(dc, ov, it->second, cx, cy - hh - 4, 0, b.id, edgeFill, cream);
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
                if (!camera::projectToScreen(cam, {gp.x, gp.y, gp.z}, gx, gy)) continue;
                int px = (int)std::lround(gx), py = (int)std::lround(gy);
                drawChip(dc, ov, population::EV_GRANARY, px - CHIP / 2, py - CHIP - 10, edgeFill,
                         cream);
                ov.markHits.push_back({px - CHIP / 2, py - CHIP - 10, CHIP, CHIP, st.id, 0});
                HGDIOBJ op = SelectObject(dc, edgeFill);
                PatBlt(dc, px, py - 10, 1, 8, PATCOPY); // a stem down to the store
                SelectObject(dc, op);
            }
        }
    }
    SelectObject(dc, oldPen);
}

// The overlay is a function of the camera, the world and the window, so it
// only needs redrawing when one of those moves. Repainting and uploading it
// costs about 2.4 ms, which is a quarter of a frame to spend on a picture
// that usually has not changed.
inline bool stale(State& ov, const world::World& w, const camera::Camera& cam, int screen) {
    const population::Field& pf = w.pop;
    bool same = ov.lastLat == cam.lat && ov.lastLon == cam.lon && ov.lastAlt == cam.altitude &&
                ov.lastW == cam.width && ov.lastH == cam.height && ov.lastT == w.simTime &&
                ov.lastSettlements == (int)pf.settlements.size() &&
                ov.lastBands == (int)pf.bands.size() && ov.lastScreen == screen;
    if (same) return false;
    ov.lastLat = cam.lat;
    ov.lastLon = cam.lon;
    ov.lastAlt = cam.altitude;
    ov.lastW = cam.width;
    ov.lastH = cam.height;
    ov.lastT = w.simTime;
    ov.lastSettlements = (int)pf.settlements.size();
    ov.lastBands = (int)pf.bands.size();
    ov.lastScreen = screen;
    return true;
}

// Hand the image to the shader on unit 7.
inline void upload(State& ov) {
    glActiveTexture(GL_TEXTURE7);
    if (!ov.ovTex) {
        glGenTextures(1, &ov.ovTex);
        glBindTexture(GL_TEXTURE_2D, ov.ovTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_2D, ov.ovTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ov.ovW, ov.ovH, 0, 0x80E1 /*GL_BGRA*/, GL_UNSIGNED_BYTE,
                 ov.ovBits);
    glActiveTexture(GL_TEXTURE0);
}

} // namespace overlay
