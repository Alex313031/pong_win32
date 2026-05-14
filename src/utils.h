#ifndef PONGWIN32_UTILS_H_
#define PONGWIN32_UTILS_H_

#include "constants.h"
#include "framework.h"

// Typedef for accessing undocumented RtlGetNtVersionNumbers in ntdll.dll
typedef void(WINAPI* RtlGetNtVersionNumbers_t)(DWORD* pNtMajorVersion,
                                               DWORD* pNtMinorVersion,
                                               DWORD* pNtBuildNumber);

extern bool g_debug_mode;

// Back-buffer GDI handles. Defined in main.cc; managed by EnsureBackBuffer
// / DestroyBackBuffer there. SaveClientBitmap reads from g_hbmMem to take
// the click-time snapshot.
extern HDC g_hdcMem;
extern HBITMAP g_hbmMem;

// Save client area as a .BMP photo, capturing moment menu was clicked. On
// success, if outSavedPath is non-null, the chosen path is written there so
// the caller can surface it (e.g. via UserMessage); pass nullptr to skip.
bool SaveClientBitmap(HWND hWnd, std::wstring* outSavedPath);

// Gets the desired font at the specified size (in pixels). Face name
// defaults to Tahoma; `italic` defaults to true to keep the existing
// marquee call sites italic without having to spell it out at every
// call. Caller owns the returned HFONT and must DeleteObject it when
// done. Returns nullptr on failure.
HFONT GetFont(int size, std::wstring font = L"Tahoma", bool italic = false);

// Fills a rect with a solid color. Wraps the CreateSolidBrush + FillRect
// + DeleteObject trio so call sites don't have to repeat all three (and
// can't forget the DeleteObject and leak a GDI brush).
bool FillRectWithColor(HDC hdc, const RECT& rc, COLORREF color);

// Fills a convex polygon with a solid color. Pen and brush share the color
// so the rasterized outline doesn't leave a 1px halo around the fill.
void FillPolygon(HDC hdc, const POINT* pts, int count, COLORREF color);

// Fills a rect with a vertical gradient: rc.top maps to topColor and
// rc.bottom maps to bottomColor, linearly interpolated per scan line.
// One row per pixel - simple and avoids needing GdiGradientFill /
// msimg32. Slightly wasteful (one brush per row) but trivial at the
// scales we draw at.
void FillRectWithGradient(HDC hdc,
                          const RECT& rc,
                          COLORREF topColor,
                          COLORREF bottomColor);

// Gets the current side by side directory, regardless of where .exe is started from
const std::wstring GetExeDir();

// Helper functions for MessageBoxW
bool InfoBox(HWND hWnd, const std::wstring& title, const std::wstring& message);

bool WarnBox(HWND hWnd, const std::wstring& title, const std::wstring& message);

bool ErrorBox(HWND hWnd, const std::wstring& title, const std::wstring& message);

// Gets version as human readable wstring.
const std::wstring GetVersionString();

// Returns APP_NAME as wstring, for easier usage.
const std::wstring GetAppName();

// Returns true on Windows XP (5.1) or later, false on Windows 2000 (5.0)
// or earlier. Used to gate styles / APIs that exist only on WinXP.
bool IsWindowsXpOrLater();

// For checking system's commctl32.dll
bool IsCommCtrlAtLeast(const DWORD to_compare);

#endif // PONGWIN32_UTILS_H_
