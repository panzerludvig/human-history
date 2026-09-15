// Selection panels: a detail window for one settlement or band, opened by
// clicking its marker. Settlement panels are tabbed and custom-painted so
// tabs and technology rows are clickable; every panel is repositioned by
// dragging anywhere that isn't a click target. Technical/Globe Viewer.md
// §Selection panels describes them; the text of each tab comes from
// inspect.h. The window procedure itself stays in main.cpp and calls the
// handlers here.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "theme.h"
#include "world.h"
#include "inspect.h"

namespace panels {

struct Panel {
    HWND wnd = nullptr;
    int kind = 0;     // 0 settlement, 1 band
    uint32_t sid = 0; // settlement identity (indices shift when one moves away)
    uint32_t bandId = 0;
    int tab = 0;      // 0 environment, 1 technology, 2 buildings
    int techSel = -1; // selected tech in the tech tab, -1 = overview list
};

// Layout constants shared by painting and hit-testing; tab hit slots are
// fixed x-ranges so no text measuring is needed to route a click.
constexpr int PANEL_W = 470, PANEL_H = 330, PANEL_BAND_H = 260;
constexpr int PANEL_PAD = 12, PANEL_TAB_Y = 36, PANEL_TAB_H = 24, PANEL_CONTENT_Y = 70,
              PANEL_LINE_H = 21;
// Slot starts, plus the right edge as a final entry: painting draws each
// label at its slot and hit-testing takes the span up to the next, so the
// two cannot disagree.
constexpr int PANEL_TAB_X[6] = {12, 74, 176, 270, 356, 420};
constexpr const char* PANEL_TABS[5] = {"People", "Environment", "Technology", "Buildings",
                                       "History"};
constexpr int PANEL_NTABS = 5;
enum : int { TAB_PEOPLE = 0, TAB_ENV, TAB_TECH, TAB_BUILT, TAB_HISTORY };

// The open panels, the cascade offset for the next one, and the panel being
// dragged with its grab offset.
struct State {
    std::vector<Panel> windows;
    int spawn = 0;
    HWND drag = nullptr;
    POINT dragOff{};
};

// The text of the panel's current tab.
inline std::string content(const world::World& w, const Panel& pn) {
    if (pn.kind == 1) return inspect::bandText(w, pn.bandId);
    int idx = inspect::settlementIndexById(w.pop, pn.sid);
    if (idx < 0) return "This settlement is gone:\nthey picked up and moved on.";
    const population::Settlement& st = w.pop.settlements[idx];
    if (pn.tab == TAB_PEOPLE) return inspect::peopleText(w, st);
    if (pn.tab == TAB_ENV) return inspect::envText(w, st);
    if (pn.tab == TAB_BUILT) return inspect::buildingsText(st, w.simTime);
    if (pn.tab == TAB_HISTORY) return inspect::historyText(w, st);
    if (pn.techSel >= 0) return inspect::techDetailText(w, st, idx, pn.techSel);
    std::string out;
    for (int t = 0; t < population::NTECH; t++)
        out += inspect::techStateLine(st.tech[t], t, w.simTime) + "\n";
    out += "\n(click a technology for details)";
    return out;
}

inline Panel* panelFor(State& st, HWND h) {
    for (Panel& p : st.windows)
        if (p.wnd == h) return &p;
    return nullptr;
}

inline void paint(HWND h, State& st, const world::World& w, const theme::Theme& th) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    FillRect(dc, &rc, th.bgBrush);
    SetBkMode(dc, TRANSPARENT);
    Panel* pn = panelFor(st, h);
    if (pn) {
        char title[48];
        if (pn->kind == 0) {
            int si = inspect::settlementIndexById(w.pop, pn->sid);
            const char* nm = si >= 0 ? w.pop.settlements[si].name : "";
            snprintf(title, sizeof title, "%s", nm[0] ? nm : "Settlement");
        } else {
            const char* nm = "";
            const char* what = "Band";
            for (const population::Band& bd : w.pop.bands)
                if (bd.id == pn->bandId) {
                    nm = bd.name;
                    what = bd.purpose == population::BAND_RAID ? "raiders"
                           : bd.colonists                      ? "colonists"
                                                               : "on the move";
                }
            if (nm[0])
                snprintf(title, sizeof title, "%s %s", nm, what);
            else
                snprintf(title, sizeof title, "Band %u", pn->bandId);
        }
        SelectObject(dc, th.panelBold);
        SetTextColor(dc, RGB(235, 235, 240));
        TextOutA(dc, PANEL_PAD, 8, title, (int)strlen(title));
        SelectObject(dc, th.panelFont);
        if (pn->kind == 0)
            for (int t = 0; t < PANEL_NTABS; t++) {
                SetTextColor(dc, pn->tab == t ? RGB(255, 225, 150) : RGB(140, 140, 155));
                TextOutA(dc, PANEL_TAB_X[t], PANEL_TAB_Y, PANEL_TABS[t],
                         (int)strlen(PANEL_TABS[t]));
            }
        std::string txt = content(w, *pn);
        int y = pn->kind == 0 ? PANEL_CONTENT_Y : PANEL_TAB_Y;
        int row = 0;
        size_t pos = 0;
        while (pos <= txt.size()) {
            size_t e = txt.find('\n', pos);
            std::string line =
                txt.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
            bool clickable =
                pn->kind == 0 && pn->tab == TAB_TECH &&
                ((pn->techSel < 0 && row < population::NTECH) || (pn->techSel >= 0 && row == 0));
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

inline void refreshAll(const State& st) {
    for (const Panel& pn : st.windows) InvalidateRect(pn.wnd, nullptr, TRUE);
}

inline void closeAll(State& st) {
    std::vector<Panel> windows = st.windows; // DestroyWindow mutates st.windows
    for (const Panel& pn : windows)
        if (IsWindow(pn.wnd)) DestroyWindow(pn.wnd);
    st.windows.clear();
    st.spawn = 0;
}

// WM_DESTROY: forget the panel, and any drag it was in the middle of.
inline void onDestroy(State& st, HWND h) {
    if (st.drag == h) {
        ReleaseCapture();
        st.drag = nullptr;
    }
    for (size_t i = 0; i < st.windows.size(); i++)
        if (st.windows[i].wnd == h) {
            st.windows.erase(st.windows.begin() + i);
            break;
        }
}

// WM_LBUTTONDOWN at (mx, my): a tab, a technology row, or else a grab.
inline void onLeftDown(State& st, HWND h, int mx, int my) {
    Panel* pn = panelFor(st, h);
    SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); // raise on grab
    if (pn && pn->kind == 0 && my >= PANEL_TAB_Y && my < PANEL_TAB_Y + PANEL_TAB_H) {
        for (int t = 0; t < PANEL_NTABS; t++)
            if (mx >= PANEL_TAB_X[t] && mx < PANEL_TAB_X[t + 1] - 6) {
                pn->tab = t;
                pn->techSel = -1;
                InvalidateRect(h, nullptr, TRUE);
                return;
            }
    }
    if (pn && pn->kind == 0 && pn->tab == TAB_TECH && my >= PANEL_CONTENT_Y) {
        int row = (my - PANEL_CONTENT_Y) / PANEL_LINE_H;
        if (pn->techSel < 0 && row >= 0 && row < population::NTECH) {
            pn->techSel = row;
            InvalidateRect(h, nullptr, TRUE);
            return;
        }
        if (pn->techSel >= 0 && row == 0) { // "< back"
            pn->techSel = -1;
            InvalidateRect(h, nullptr, TRUE);
            return;
        }
    }
    // Anywhere else grabs the window for dragging.
    SetCapture(h);
    st.drag = h;
    st.dragOff = {mx, my};
}

// WM_MOUSEMOVE: a grabbed panel follows the cursor, in the parent's space.
inline void onMouseMove(const State& st, HWND h, HWND parent) {
    if (st.drag != h) return;
    POINT c;
    GetCursorPos(&c);
    ScreenToClient(parent, &c);
    SetWindowPos(h, nullptr, c.x - st.dragOff.x, c.y - st.dragOff.y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER);
}

inline void onLeftUp(State& st, HWND h) {
    if (st.drag != h) return;
    ReleaseCapture();
    st.drag = nullptr;
}

// Open a panel for a settlement (kind 0, by sid) or a band (kind 1, by id),
// on the given tab if any; an already open one is raised instead of
// stacking a twin. The window is of class "IBPanel", registered in main.cpp.
inline void open(State& st, HWND parent, HFONT font, int kind, uint32_t sid, uint32_t bandId,
                 int tab) {
    for (Panel& pn : st.windows)
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
    int x = 16 + (st.spawn % 7) * 30, y = 56 + (st.spawn % 7) * 30;
    st.spawn++;
    HINSTANCE inst = GetModuleHandleA(nullptr);
    HWND w = CreateWindowA("IBPanel", "", WS_CHILD | WS_BORDER | WS_VISIBLE | WS_CLIPSIBLINGS, x, y,
                           pw, ph, parent, nullptr, inst, nullptr);
    HWND btn = CreateWindowA("BUTTON", "X", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, pw - 34, 6, 24,
                             24, w, (HMENU)1, inst, nullptr);
    SendMessageA(btn, WM_SETFONT, (WPARAM)font, TRUE);
    st.windows.push_back({w, kind, sid, bandId, tab > 0 ? tab : 0});
    SetWindowPos(w, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

} // namespace panels
