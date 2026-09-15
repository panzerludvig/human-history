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
#include "overlay.h"
#include "theme.h"
#include "menus.h"

// Generation-stage feedback on the menu status line. The build runs on the
// UI thread, so the label is repainted synchronously.
static void buildProgress(const char* stage);

// ---------------------------------------------------------------- app state

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
    menus::Screen screen = menus::Screen::MainMenu;
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
    overlay::State overlay;
    bool running = true;
    std::string shotPath; // when set, save the next rendered frame here (testing)
    int debugMode = 0; // 0 normal, 1 plates, 2 substrate, 3 vegetation
    int octaves = 8;   // current level of detail, shared with the tooltip
    HWND hwnd = nullptr;
    theme::Theme theme;
    HWND panelDrag = nullptr; // panel being dragged, with the grab offset
    POINT panelDragOff{};
    HWND news = nullptr;   // the feed down the right-hand side
    int newsLevel = 0;     // 0 kinds, 1 the entries of one kind, 2 one entry
    int newsKind = 0;      // which kind is open
    int newsPick = 0;      // which entry is open
    int newsScroll = 0;
    bool newsOpen = true;  // collapsed to a tab on the right edge when false
    menus::State menu;
};
static App app;

static void advanceDays(double days);
static void updateDateLabel();
static void closeAllPanels();
static void refreshPanels();

static void buildProgress(const char* stage) {
    fprintf(stderr, "build: %s%c", stage, 10);
    // Skip when the status line is not on screen (e.g. the argv test path).
    HWND st = menus::control(app.menu, menus::ID_STATUS);
    if (!st || !IsWindowVisible(st)) return;
    SetWindowTextA(st, stage);
    UpdateWindow(st);
}

static void setScreen(menus::Screen s) {
    app.screen = s;
    ShowWindow(menus::control(app.menu, menus::ID_TOOLTIP), SW_HIDE);
    if (s != menus::Screen::InGame) closeAllPanels();
    app.dragging = false;
    if (s == menus::Screen::LoadMenu) menus::refreshWorldList(app.menu, savefile::list());
    if (s == menus::Screen::NewWorldMenu) menus::fillNewWorldFields(app.menu, app.world);
    if (s == menus::Screen::PauseMenu)
        SetWindowTextA(menus::control(app.menu, menus::ID_SAVE_NAME), app.world.name.c_str());
    menus::layoutControls(app.menu, app.screen, app.cam.width, app.cam.height);
    if (app.news) ShowWindow(app.news, s == menus::Screen::InGame ? SW_SHOW : SW_HIDE);
    if (s == menus::Screen::InGame) {
        updateDateLabel();
        SetFocus(app.hwnd);
    }
    if (s == menus::Screen::PauseMenu) {
        HWND edit = menus::control(app.menu, menus::ID_SAVE_NAME);
        SetFocus(edit);
        SendMessageA(edit, EM_SETSEL, 0, -1);
    }
}

static uint32_t randomSeed() { return (uint32_t)std::random_device{}(); }

static void openNewWorldMenu() {
    app.world = world::World{};
    app.world.seed = randomSeed();
    menus::setStatus(app.menu, "");
    setScreen(menus::Screen::NewWorldMenu);
}

static void generateWorld() {
    world::World w;
    {
        // A seed reading "earth", in any case, is the template globe.
        char sb[64];
        GetWindowTextA(menus::control(app.menu, menus::ID_GEN_SEED), sb, sizeof sb);
        std::string st = sb;
        for (char& ch : st) ch = (char)tolower((unsigned char)ch);
        w.earth = st.find("earth") != std::string::npos;
        w.seed = w.earth ? 1u : (uint32_t)std::clamp(atof(sb), 0.0, 4294967295.0);
    }
    w.landPercent =
        (float)std::clamp(menus::getEditNumber(app.menu, menus::ID_GEN_LAND), 0.0, 100.0);
    w.concentration =
        (float)std::clamp(menus::getEditNumber(app.menu, menus::ID_GEN_CONC), 0.0, 100.0);
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
        menus::setStatus(app.menu, "Could not load " + app.genName);
        return;
    }
    textures::uploadAll(app.tex, app.world);
    if (app.genKind == 0) {
        app.cam.lat = 0.35;
        app.cam.lon = 0.0;
        app.cam.altitude = app.cam.maxAltitude();
    }
    menus::setStatus(app.menu, "");
    setScreen(menus::Screen::InGame);
}

static void onCommand(int id) {
    if (app.genState != 0) return; // generation in progress: only the OS window moves
    switch (id) {
    case menus::ID_NEW_WORLD:
        openNewWorldMenu();
        break;
    case menus::ID_GEN_CREATE:
        generateWorld();
        break;
    case menus::ID_GEN_BACK:
        setScreen(menus::Screen::MainMenu);
        break;
    case menus::ID_GEN_RANDOM:
        menus::setEditNumber(app.menu, menus::ID_GEN_SEED, (double)randomSeed());
        break;
    case menus::ID_LOAD_WORLD:
        menus::setStatus(app.menu, "");
        setScreen(menus::Screen::LoadMenu);
        break;
    case menus::ID_QUIT:
    case menus::ID_PAUSE_QUIT:
        app.running = false;
        break;
    case menus::ID_LOAD_BACK:
        setScreen(menus::Screen::MainMenu);
        break;
    case menus::ID_LOAD_CONFIRM: {
        std::string name = menus::selectedWorld(app.menu);
        if (name.empty()) {
            menus::setStatus(app.menu, "No saved worlds");
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
    case menus::ID_LOAD_DELETE: {
        std::string name = menus::selectedWorld(app.menu);
        if (name.empty()) {
            menus::setStatus(app.menu, "No saved worlds");
            break;
        }
        std::string q = "Delete world \"" + name + "\"? This cannot be undone.";
        if (MessageBoxA(app.hwnd, q.c_str(), "Delete World", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            break;
        std::string path = savefile::worldsDir() + "\\" + name + ".ibw";
        menus::setStatus(app.menu, DeleteFileA(path.c_str()) ? "Deleted " + name
                                                             : "Could not delete " + name);
        menus::refreshWorldList(app.menu, savefile::list());
        break;
    }
    case menus::ID_SAVE_WORLD: {
        char buf[128];
        GetWindowTextA(menus::control(app.menu, menus::ID_SAVE_NAME), buf, sizeof buf);
        std::string name = menus::sanitizeName(buf);
        if (name.empty()) {
            menus::setStatus(app.menu, "Enter a name for the world");
            break;
        }
        app.world.name = name;
        SetWindowTextA(menus::control(app.menu, menus::ID_SAVE_NAME), name.c_str());
        menus::setStatus(app.menu,
                         savefile::save(app.world, app.cam) ? "Saved as " + name : "Save failed");
        break;
    }
    case menus::ID_TIME_GO: {
        static const double stepDays[] = {1.0 / 1440.0, 1.0 / 24.0, 1.0, 30.0, 365.0, 3650.0, 36500.0};
        int sel =
            (int)SendMessageA(menus::control(app.menu, menus::ID_TIME_STEP), CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < 7) advanceDays(stepDays[sel]);
        SetFocus(app.hwnd);
        break;
    }
    case menus::ID_MAIN_MENU:
        menus::setStatus(app.menu, "");
        setScreen(menus::Screen::MainMenu);
        break;
    }
}

inline int newsWidth(); // defined with the news feed, below

static void updateTooltip(int x, int y) {
    HWND tip = menus::control(app.menu, menus::ID_TOOLTIP);
    camera::Vec3 hit;
    if (app.screen != menus::Screen::InGame || !app.cam.hitSphere(x, y, hit)) {
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
    FillRect(dc, &rc, app.theme.bgBrush);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, app.theme.panelBold);
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
    SelectObject(dc, app.theme.panelFont);
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
    ShowWindow(app.news, app.screen == menus::Screen::InGame ? SW_SHOW : SW_HIDE);
    InvalidateRect(app.news, nullptr, TRUE);
}

static void paintPanel(HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    FillRect(dc, &rc, app.theme.bgBrush);
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
        SelectObject(dc, app.theme.panelBold);
        SetTextColor(dc, RGB(235, 235, 240));
        TextOutA(dc, PANEL_PAD, 8, title, (int)strlen(title));
        SelectObject(dc, app.theme.panelFont);
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
    SendMessageA(btn, WM_SETFONT, (WPARAM)app.theme.font, TRUE);
    app.panels.push_back({w, kind, sid, bandId, tab > 0 ? tab : 0});
    SetWindowPos(w, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

// A click on the globe: open a detail panel for the settlement or band whose
// marker is under the cursor (same radii the shader draws them with).
static void pickAt(int x, int y) {
    if (app.screen != menus::Screen::InGame) return;
    // An event mark is hit before anything under it: it sits above the
    // marker on purpose, and it is the smaller target. It opens the history
    // of whoever it happened to, which is the whole sentence the news feed
    // would have given.
    for (const overlay::MarkHit& m : app.overlay.markHits)
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
    float sRadius =
        kmpp < overlay::HUT_KMPP ? 0.15f : (float)(kmpp * (overlay::markerRadius(kmpp) + 3));
    int best = -1;
    float bestD = sRadius;
    for (int i = 0; i < (int)pf.settlements.size(); i++) {
        float d = sim::distKm(n, sim::cellCentre(pf.settlements[i].cell));
        if (d < bestD) { bestD = d; best = i; }
    }
    if (best >= 0) { openPanel(0, pf.settlements[best].id, 0); return; }
    float bRadius =
        kmpp < overlay::WALK_KMPP ? 0.08f : (float)(kmpp * (overlay::markerRadius(kmpp) + 3));
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
    SetWindowTextA(menus::control(app.menu, menus::ID_DATE_LABEL), simDate().c_str());
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
    case menus::Screen::InGame:
        setScreen(menus::Screen::PauseMenu);
        break;
    case menus::Screen::PauseMenu:
        setScreen(menus::Screen::InGame);
        break;
    case menus::Screen::LoadMenu:
    case menus::Screen::NewWorldMenu:
        setScreen(menus::Screen::MainMenu);
        break;
    case menus::Screen::MainMenu:
        break;
    }
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        app.cam.width = std::max(1, (int)LOWORD(lp));
        app.cam.height = std::max(1, (int)HIWORD(lp));
        app.cam.clampAltitude();
        glViewport(0, 0, app.cam.width, app.cam.height);
        if (!app.menu.controls.empty())
            menus::layoutControls(app.menu, app.screen, app.cam.width, app.cam.height);
        if (app.news)
            layoutNews();
        return 0;
    case WM_COMMAND:
        if (HIWORD(wp) == BN_CLICKED) onCommand(LOWORD(wp));
        else if (HIWORD(wp) == LBN_DBLCLK)
            onCommand(menus::ID_LOAD_CONFIRM);
        return 0;
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wp, RGB(230, 230, 235));
        SetBkColor((HDC)wp, RGB(8, 8, 16));
        return (LRESULT)app.theme.bgBrush;
    case WM_LBUTTONDOWN:
        if (app.screen != menus::Screen::InGame) return 0;
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
        if (app.screen != menus::Screen::InGame) return 0;
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
        if (app.screen == menus::Screen::InGame) {
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

    app.theme = theme::create();
    overlay::createFonts(app.overlay);
    menus::createControls(app.menu, app.hwnd, app.theme);
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
        setScreen(menus::Screen::InGame);
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
        setScreen(menus::Screen::MainMenu);
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
                if (app.screen == menus::Screen::PauseMenu) {
                    onCommand(menus::ID_SAVE_WORLD);
                    continue;
                }
                if (app.screen == menus::Screen::NewWorldMenu) {
                    onCommand(menus::ID_GEN_CREATE);
                    continue;
                }
            }
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
        bool showGlobe =
            app.screen == menus::Screen::InGame || app.screen == menus::Screen::PauseMenu;
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
            glUniform1f(uDim, app.screen == menus::Screen::PauseMenu ? 0.35f : 1.0f);
            glUniform1f(uFreq, app.world.cp.freq);
            glUniform1f(uWarp, app.world.cp.warp);
            glUniform1f(uWebness, app.world.cp.webness);
            glUniform1f(uSeaLevel, app.world.seaLevel);
            glUniform1i(uHasHydro, app.world.hydro.cells.empty() ? 0 : 1);
            glUniform1i(uUseEarth, app.world.earth && terrain::TEMPLATE.active ? 1 : 0);
            glUniform1i(uDebugMode, app.debugMode);
            if (app.screen == menus::Screen::InGame) {
                menus::ScaleBar sb = menus::chooseScale(kmpp);
                // Pixel coordinates with origin bottom-left, as gl_FragCoord uses.
                float x0 = (float)menus::SCALE_MARGIN, y0 = (float)menus::SCALE_MARGIN + 6;
                glUniform4f(uScaleBar, x0, y0, x0 + sb.px, y0);
                std::string txt = menus::scaleText(sb.km);
                if (txt != lastScaleText) {
                    lastScaleText = txt;
                    SetWindowTextA(menus::control(app.menu, menus::ID_SCALE_LABEL), txt.c_str());
                }
            } else {
                glUniform4f(uScaleBar, -1, -1, -1, -1);
            }
            // Markers and names: redrawn every frame, since they follow the
            // camera as much as the world.
            if (overlay::stale(app.overlay, app.world, app.cam, (int)app.screen)) {
                overlay::paint(app.overlay, app.world, app.cam,
                               app.screen == menus::Screen::InGame);
                overlay::upload(app.overlay);
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
