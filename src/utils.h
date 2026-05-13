#ifndef PONGWIN32_UTILS_H_
#define PONGWIN32_UTILS_H_

#include "framework.h"

// Typedef for accessing undocumented RtlGetNtVersionNumbers in ntdll.dll
typedef void(WINAPI* RtlGetNtVersionNumbers_t)(DWORD* pNtMajorVersion,
                                               DWORD* pNtMinorVersion,
                                               DWORD* pNtBuildNumber);
// Color constants
#define RGB_BLACK   RGB(0, 0, 0)
#define RGB_WHITE   RGB(255, 255, 255)
#define RGB_GREY    RGB(128, 128, 128)
#define RGB_LTGREY  RGB(192, 192, 192) // Classic Win9x/2000 button-face grey
#define RGB_RED     RGB(255, 0, 0)
#define RGB_GREEN   RGB(0, 255, 0)
#define RGB_BLUE    RGB(0, 0, 255)
#define RGB_YELLOW  RGB(255, 255, 0)
#define RGB_CYAN    RGB(0, 255, 255)
#define RGB_MAGENTA RGB(255, 0, 255)

inline constexpr UINT kGameTickDelay = static_cast<UINT>(std::round(16.7f));

// Default desired ant canvas size (NOT the outer window size). wWinMain
// adds the OS chrome and the toolbar's measured height on top of these
// to compute the actual outer window size, so the user always gets a
// CW_WIDTH x CW_HEIGHT game canvas at startup regardless of how tall the
// menu bar / toolbar end up being.
inline constexpr INT CW_WIDTH  = 1024;
inline constexpr INT CW_HEIGHT = 800;

// Min window size
inline constexpr INT CW_MINWIDTH  = 640;
inline constexpr INT CW_MINHEIGHT = 480;

// Child window style
inline constexpr DWORD dwCHILD = WS_CHILD | WS_VISIBLE;

// Minimum common controls version for certain functions, used for fallback codepaths
// See https://learn.microsoft.com/en-us/windows/win32/controls/common-control-versions
inline constexpr DWORD dwComCtl32TargetVer =
    _PACKVERSION(static_cast<DWORD>(5u), static_cast<DWORD>(82u));

extern bool g_debug_mode;

// Bitmap buffer
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
