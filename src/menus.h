// The menus: native Win32 controls laid over the GL window and shown or
// hidden per screen -- the main menu, the New World form, the load list,
// the pause menu, and the in-game scale label, tooltip and time controls.
// Technical/Globe Viewer.md §Menus and worlds and §Scale bar describe them.
// What a control does when pressed is wired in main.cpp; this header owns
// what exists and where it sits.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>
#include "theme.h"
#include "world.h"

namespace menus {

// The controls, by id, and the window they are children of.
struct State {
    HWND parent = nullptr;
    std::vector<std::pair<int, HWND>> controls;
};

enum class Screen { MainMenu, NewWorldMenu, LoadMenu, InGame, PauseMenu };

// Control IDs for the Win32 controls that make up the menus.
enum : int {
    ID_NEW_WORLD = 100,
    ID_LOAD_WORLD,
    ID_QUIT,
    ID_LOAD_LIST,
    ID_LOAD_CONFIRM,
    ID_LOAD_DELETE,
    ID_LOAD_BACK,
    ID_SAVE_NAME,
    ID_SAVE_WORLD,
    ID_MAIN_MENU,
    ID_PAUSE_QUIT,
    ID_TITLE,
    ID_STATUS,
    ID_GEN_SEED_LABEL,
    ID_GEN_SEED,
    ID_GEN_RANDOM,
    ID_GEN_LAND_LABEL,
    ID_GEN_LAND,
    ID_GEN_CONC_LABEL,
    ID_GEN_CONC,
    ID_GEN_HINT,
    ID_GEN_CREATE,
    ID_GEN_BACK,
    ID_SCALE_LABEL,
    ID_TOOLTIP,
    ID_TIME_STEP,
    ID_TIME_GO,
    ID_DATE_LABEL,
};

// The window of a control by id, or null before the controls exist.
inline HWND control(const State& st, int id) {
    for (const auto& c : st.controls)
        if (c.first == id) return c.second;
    return nullptr;
}

inline void setStatus(const State& st, const std::string& s) {
    SetWindowTextA(control(st, ID_STATUS), s.c_str());
}

inline void addControl(State& st, const theme::Theme& th, int id, const char* cls, const char* text,
                       DWORD style) {
    HWND h = CreateWindowA(cls, text, WS_CHILD | style, 0, 0, 10, 10, st.parent, (HMENU)(INT_PTR)id,
                           GetModuleHandleA(nullptr), nullptr);
    SendMessageA(h, WM_SETFONT, (WPARAM)(id == ID_TITLE ? th.titleFont : th.font), TRUE);
    st.controls.push_back({id, h});
}

// Every control of every screen, hidden; layoutControls shows a screen's.
inline void createControls(State& st, HWND parent, const theme::Theme& th) {
    st.parent = parent;
    addControl(st, th, ID_TITLE, "STATIC", "Human History", SS_CENTER);
    addControl(st, th, ID_STATUS, "STATIC", "", SS_CENTER);
    addControl(st, th, ID_NEW_WORLD, "BUTTON", "New World", BS_PUSHBUTTON);
    addControl(st, th, ID_LOAD_WORLD, "BUTTON", "Load World", BS_PUSHBUTTON);
    addControl(st, th, ID_QUIT, "BUTTON", "Quit", BS_PUSHBUTTON);
    addControl(st, th, ID_LOAD_LIST, "LISTBOX", "", WS_BORDER | WS_VSCROLL | LBS_NOTIFY);
    addControl(st, th, ID_LOAD_CONFIRM, "BUTTON", "Load", BS_PUSHBUTTON);
    addControl(st, th, ID_LOAD_DELETE, "BUTTON", "Delete", BS_PUSHBUTTON);
    addControl(st, th, ID_LOAD_BACK, "BUTTON", "Back", BS_PUSHBUTTON);
    addControl(st, th, ID_SAVE_NAME, "EDIT", "", WS_BORDER | ES_AUTOHSCROLL | ES_CENTER);
    SendMessageA(control(st, ID_SAVE_NAME), EM_SETLIMITTEXT, 64, 0);
    addControl(st, th, ID_SAVE_WORLD, "BUTTON", "Save World", BS_PUSHBUTTON);
    addControl(st, th, ID_MAIN_MENU, "BUTTON", "Main Menu", BS_PUSHBUTTON);
    addControl(st, th, ID_PAUSE_QUIT, "BUTTON", "Quit Game", BS_PUSHBUTTON);
    addControl(st, th, ID_GEN_SEED_LABEL, "STATIC", "Seed", SS_RIGHT);
    addControl(st, th, ID_GEN_SEED, "EDIT", "",
               WS_BORDER | ES_AUTOHSCROLL); // not ES_NUMBER: "earth" is a seed
    addControl(st, th, ID_GEN_RANDOM, "BUTTON", "Random", BS_PUSHBUTTON);
    addControl(st, th, ID_GEN_LAND_LABEL, "STATIC", "Land %", SS_RIGHT);
    addControl(st, th, ID_GEN_LAND, "EDIT", "", WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER);
    addControl(st, th, ID_GEN_CONC_LABEL, "STATIC", "Concentration %", SS_RIGHT);
    addControl(st, th, ID_GEN_CONC, "EDIT", "", WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER);
    addControl(st, th, ID_GEN_HINT, "STATIC",
               "Concentration: 0 = island webs and thin strips, 100 = one massive continent",
               SS_CENTER);
    addControl(st, th, ID_GEN_CREATE, "BUTTON", "Generate", BS_PUSHBUTTON);
    addControl(st, th, ID_GEN_BACK, "BUTTON", "Back", BS_PUSHBUTTON);
    addControl(st, th, ID_SCALE_LABEL, "STATIC", "", SS_LEFT);
    addControl(st, th, ID_TOOLTIP, "STATIC", "", SS_LEFT | SS_NOPREFIX);
    addControl(st, th, ID_DATE_LABEL, "STATIC", "", SS_RIGHT);
    addControl(st, th, ID_TIME_STEP, "COMBOBOX", "", CBS_DROPDOWNLIST | WS_VSCROLL);
    addControl(st, th, ID_TIME_GO, "BUTTON", "Advance", BS_PUSHBUTTON);
    {
        HWND cb = control(st, ID_TIME_STEP);
        for (const char* it :
             {"1 minute", "1 hour", "1 day", "1 month", "1 year", "10 years", "100 years"})
            SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)it);
        SendMessageA(cb, CB_SETCURSEL, 2, 0); // default: 1 day
    }
}

// Map scale bar: a 1/2/5 x 10^n distance whose bar is close to a target
// width, placed in the bottom-left corner with its label above it.
constexpr int SCALE_MARGIN = 24;
struct ScaleBar {
    double km = 0;
    int px = 0;
};
inline ScaleBar chooseScale(double kmPerPixel, int targetPx = 160) {
    double raw = kmPerPixel * targetPx;
    double mag = std::pow(10.0, std::floor(std::log10(raw)));
    double best = mag;
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (m * mag <= raw) best = m * mag;
    return {best, (int)std::lround(best / kmPerPixel)};
}
inline std::string scaleText(double km) {
    char buf[32];
    if (km >= 1.0)
        snprintf(buf, sizeof buf, "%g km", km);
    else
        snprintf(buf, sizeof buf, "%g m", km * 1000.0);
    return buf;
}

// Position and show the controls that belong to the current screen.
inline void layoutControls(const State& st, Screen screen, int W, int H) {
    const int bw = 280, bh = 48, gap = 14;
    int cx = W / 2 - bw / 2;
    for (const auto& c : st.controls) ShowWindow(c.second, SW_HIDE);

    auto place = [&](int id, int x, int y, int w, int h) {
        SetWindowPos(control(st, id), HWND_TOP, x, y, w, h, SWP_SHOWWINDOW);
    };
    auto stack = [&](std::initializer_list<int> ids, int top) {
        int y = top;
        for (int id : ids) {
            place(id, cx, y, bw, bh);
            y += bh + gap;
        }
        return y;
    };

    switch (screen) {
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

// Fill the load list with the given names and select the first.
inline void refreshWorldList(const State& st, const std::vector<std::string>& names) {
    HWND list = control(st, ID_LOAD_LIST);
    SendMessageA(list, LB_RESETCONTENT, 0, 0);
    for (const std::string& n : names) SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)n.c_str());
    SendMessageA(list, LB_SETCURSEL, 0, 0);
}

// Name of the world currently selected in the load list, or empty.
inline std::string selectedWorld(const State& st) {
    HWND list = control(st, ID_LOAD_LIST);
    int sel = (int)SendMessageA(list, LB_GETCURSEL, 0, 0);
    if (sel < 0) return "";
    char name[MAX_PATH];
    SendMessageA(list, LB_GETTEXT, sel, (LPARAM)name);
    return name;
}

// Turn whatever was typed into something that is safe as a file name.
inline std::string sanitizeName(std::string n) {
    const std::string bad = "\\/:*?\"<>|";
    for (char& ch : n)
        if (bad.find(ch) != std::string::npos || (unsigned char)ch < 32) ch = '_';
    size_t a = n.find_first_not_of(" ."), b = n.find_last_not_of(" .");
    if (a == std::string::npos) return "";
    return n.substr(a, b - a + 1);
}

inline void setEditNumber(const State& st, int id, double v, int decimals = 0) {
    char buf[64];
    snprintf(buf, sizeof buf, "%.*f", decimals, v);
    SetWindowTextA(control(st, id), buf);
}

inline double getEditNumber(const State& st, int id) {
    char buf[64];
    GetWindowTextA(control(st, id), buf, sizeof buf);
    return atof(buf);
}

inline void fillNewWorldFields(const State& st, const world::World& w) {
    if (w.earth)
        SetWindowTextA(control(st, ID_GEN_SEED), "earth");
    else
        setEditNumber(st, ID_GEN_SEED, (double)w.seed);
    setEditNumber(st, ID_GEN_LAND, w.landPercent);
    setEditNumber(st, ID_GEN_CONC, w.concentration);
}

} // namespace menus
