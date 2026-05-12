#ifndef PONGWIN32_GLOBALS_H_
#define PONGWIN32_GLOBALS_H_

#include "framework.h"

// Main client width/height
extern int cxClient;
extern int cyClient;

extern HINSTANCE g_hInstance; // This program instance, everything descends from this

extern HWND mainHwnd; // Our main window handle

extern volatile bool g_running; // Controlling global game running/stop state

extern COLORREF g_bkg_color; // Current background color, changed via the Colors menu

extern bool can_use_582_controls; // Whether we can use "modern" common controls from XP+

#endif // PONGWIN32_GLOBALS_H_
