// The viewer's look, as far as it has one: the fonts the menus, the panels
// and the news feed share, and the near-black brush behind all of them.
// Technical/Globe Viewer.md §Menus and worlds records that the menus are
// native controls with no visual identity yet; this is the one place the
// identity will grow from, so every window draws from the same handles.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace theme {

struct Theme {
    HFONT font = nullptr, titleFont = nullptr;      // menu controls and the title
    HFONT panelFont = nullptr, panelBold = nullptr; // dense panel and news text
    HBRUSH bgBrush = nullptr;                       // the background behind everything
};

// Created once, at window creation; the handles live as long as the process.
inline Theme create() {
    Theme th;
    th.font = CreateFontA(24, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                          0, "Segoe UI");
    th.titleFont = CreateFontA(56, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                               CLEARTYPE_QUALITY, 0, "Segoe UI");
    th.panelFont = CreateFontA(18, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                               CLEARTYPE_QUALITY, 0, "Segoe UI");
    th.panelBold = CreateFontA(18, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                               CLEARTYPE_QUALITY, 0, "Segoe UI");
    th.bgBrush = CreateSolidBrush(RGB(8, 8, 16));
    return th;
}

} // namespace theme
