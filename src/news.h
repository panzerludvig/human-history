// The news feed down the right-hand side: what happened while the clock
// was running, grouped by kind. Click a group to see its entries, an entry
// to see the detail, and "Go to" to put the camera on whoever it happened
// to; the chevron collapses it to a tab. Technical/Globe Viewer.md §The
// news feed describes it. The window procedure stays in main.cpp and calls
// the handlers here; click reports the event to go to, since moving the
// camera and opening a panel are the caller's business.
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
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "theme.h"
#include "world.h"
#include "inspect.h"

namespace news {

constexpr int NEWS_W = 320, NEWS_LINE = 20, NEWS_TOP = 40;
// Collapsed, the feed is a tab just wide enough for the chevron and a
// three-character count, and wide enough to be an easy click target.
constexpr int NEWS_TAB_W = 34;
// The chevron sits in the top-right of the open panel. Its hit box must
// stay clear of the title, which is the way back up a level -- these two
// x-ranges are the invariant: [12, NEWS_W-30) is the title, and
// [NEWS_W-26, NEWS_W-6) is the chevron.
constexpr int NEWS_CHEVRON_X = NEWS_W - 26, NEWS_CHEVRON_R = NEWS_W - 6;

// The feed's window and where the reader is in it.
struct State {
    HWND wnd = nullptr;
    int level = 0; // 0 kinds, 1 the entries of one kind, 2 one entry
    int kind = 0;  // which kind is open
    int pick = 0;  // which entry is open
    int scroll = 0;
    bool open = true; // collapsed to a tab on the right edge when false
};

inline int width(const State& st) { return st.open ? NEWS_W : NEWS_TAB_W; }

// Place the feed against the right edge of a W x H client area.
inline void layout(const State& st, int W, int H) {
    if (!st.wnd) return;
    int w = width(st);
    int h = st.open ? H - 76 : NEWS_TAB_W; // shut: a small square
    SetWindowPos(st.wnd, nullptr, W - w, 56, w, h, SWP_NOZORDER);
    InvalidateRect(st.wnd, nullptr, TRUE);
}

constexpr const char* NEWS_LABEL[population::EV_KINDS][2] = {
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
inline std::vector<const population::Event*> entries(const population::Field& pf, int kind) {
    std::vector<const population::Event*> v;
    for (const population::Event& e : pf.events)
        if (e.kind == kind) v.push_back(&e);
    return v;
}

inline void paint(HWND h, const State& st, const world::World& w, const theme::Theme& th) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    FillRect(dc, &rc, th.bgBrush);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, th.panelBold);
    if (!st.open) {
        // Shut, the feed is a small square with the way back in. Whether
        // there is news at all shows as the colour of the chevron.
        int total = 0;
        for (int k = 0; k < population::EV_KINDS; k++) total += w.pop.eventCount[k];
        SetTextColor(dc, total ? RGB(255, 215, 130) : RGB(140, 140, 155));
        TextOutA(dc, 12, 7, "<", 1);
        EndPaint(h, &ps);
        return;
    }
    SetTextColor(dc, RGB(235, 235, 240));
    const char* title = st.level == 0 ? "What happened" : "< back";
    TextOutA(dc, 12, 10, title, (int)strlen(title));
    SetTextColor(dc, RGB(255, 215, 130));
    TextOutA(dc, NEWS_CHEVRON_X, 10, ">", 1);
    SelectObject(dc, th.panelFont);
    int y = NEWS_TOP;
    int maxLines = (rc.bottom - NEWS_TOP) / NEWS_LINE - 1;
    if (st.level == 0) {
        bool any = false;
        for (int k = 0; k < population::EV_KINDS; k++) {
            int n = w.pop.eventCount[k];
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
    } else if (st.level == 1) {
        std::vector<const population::Event*> v = entries(w.pop, st.kind);
        for (int i = st.scroll; i < (int)v.size() && y < rc.bottom - NEWS_LINE; i++) {
            SetTextColor(dc, RGB(255, 215, 130));
            TextOutA(dc, 12, y, v[i]->text, (int)strlen(v[i]->text));
            y += NEWS_LINE;
        }
        int shown = (int)v.size() - st.scroll;
        if (shown > maxLines || w.pop.eventCount[st.kind] > (int)v.size()) {
            SetTextColor(dc, RGB(140, 140, 155));
            char more[96];
            snprintf(more, sizeof more, "(%d of %d; scroll with the wheel)",
                     std::min(shown, maxLines), w.pop.eventCount[st.kind]);
            TextOutA(dc, 12, rc.bottom - NEWS_LINE, more, (int)strlen(more));
        }
    } else {
        std::vector<const population::Event*> v = entries(w.pop, st.kind);
        if (st.pick < (int)v.size()) {
            const population::Event& e = *v[st.pick];
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
            if (l2.size()) {
                TextOutA(dc, 12, y, l2.c_str(), (int)l2.size());
                y += NEWS_LINE;
            }
            int si = inspect::settlementIndexById(w.pop, e.sid),
                s2 = inspect::settlementIndexById(w.pop, e.sid2);
            if (si >= 0) {
                snprintf(line, sizeof line, "Who: %s", w.pop.settlements[si].name);
                TextOutA(dc, 12, y, line, (int)strlen(line));
                y += NEWS_LINE;
            }
            if (s2 >= 0) {
                snprintf(line, sizeof line, "Other party: %s", w.pop.settlements[s2].name);
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
                for (const population::Band& b : w.pop.bands)
                    if (b.id == e.bandId) alive = true;
                snprintf(line, sizeof line, "%s",
                         alive ? "The band is still out there."
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

// WM_MOUSEWHEEL: scroll the entry list by whole notches.
inline void wheel(State& st, HWND h, int delta) {
    if (st.level != 1) return;
    st.scroll = std::max(0, st.scroll - delta / 120);
    InvalidateRect(h, nullptr, TRUE);
}

// WM_LBUTTONDOWN at (mx, my), in a W x H client area: opens or shuts the
// feed, steps up or down a level, and returns the event whose "Go to" was
// hit, or null.
inline const population::Event* click(State& st, HWND h, const population::Field& pf, int mx,
                                      int my, int W, int H) {
    if (!st.open) { // the whole tab opens it again
        st.open = true;
        layout(st, W, H);
        return nullptr;
    }
    if (my < NEWS_TOP && mx >= NEWS_CHEVRON_X && mx < NEWS_CHEVRON_R) {
        st.open = false; // out of the way, without losing the news
        layout(st, W, H);
        return nullptr;
    }
    if (my < NEWS_TOP) { // the title doubles as the way back
        if (st.level > 0) st.level--;
        st.scroll = 0;
        InvalidateRect(h, nullptr, TRUE);
        return nullptr;
    }
    const population::Event* goTo = nullptr;
    int row = (my - NEWS_TOP) / NEWS_LINE;
    if (st.level == 0) {
        int seen = 0;
        for (int k = 0; k < population::EV_KINDS; k++) {
            if (!pf.eventCount[k]) continue;
            if (seen == row) {
                st.kind = k;
                st.level = 1;
                st.scroll = 0;
                break;
            }
            seen++;
        }
    } else if (st.level == 1) {
        std::vector<const population::Event*> v = entries(pf, st.kind);
        int idx = st.scroll + row;
        if (idx < (int)v.size()) {
            st.pick = idx;
            st.level = 2;
        }
    } else {
        std::vector<const population::Event*> v = entries(pf, st.kind);
        if (st.pick < (int)v.size()) goTo = v[st.pick];
    }
    InvalidateRect(h, nullptr, TRUE);
    return goTo;
}

// After a time step: back to the list of kinds, shown only in game.
inline void refresh(State& st, bool inGame) {
    if (!st.wnd) return;
    st.level = 0;
    st.scroll = 0;
    ShowWindow(st.wnd, inGame ? SW_SHOW : SW_HIDE);
    InvalidateRect(st.wnd, nullptr, TRUE);
}

} // namespace news
