#include "utils.h"

#include <shlwapi.h>

#include "globals.h"
#include "resource.h"

HBITMAP g_hbmMem = nullptr;

// Opens a system Save As dialog and writes the current back buffer to a 32-bit
// BMP file at the path the user chose. On success, if outSavedPath is non-null,
// the chosen path is written there so the caller can surface it to the user
// (e.g. via UserMessage). Pass nullptr to skip.
//
// BMP layout (no palette for 32-bit):
//   BITMAPFILEHEADER  (14 bytes) - magic 'BM', file size, pixel data offset
//   BITMAPINFOHEADER  (40 bytes) - dimensions, bit depth, compression
//   Pixel data        (w * h * 4 bytes) - 32-bit BGRA, bottom-up row order
bool SaveClientBitmap(HWND hWnd, std::wstring* outSavedPath) {
  return true; // TODO
}

HFONT GetFont(int size, std::wstring font, bool italic) {
  // Negative height = "character height" in logical units (the cap
  // box), so passing -size yields ~size-pixel-tall glyphs on a
  // standard MM_TEXT DC. ANTIALIASED_QUALITY keeps big text from
  // looking jagged - the rest of the app embraces a retro aliased
  // look but 72-px text without smoothing is unreadable.
  return CreateFontW(-size, 0, 0, 0, FW_NORMAL, italic ? TRUE : FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                     font.c_str());
}

bool FillRectWithColor(HDC hdc, const RECT& rc, COLORREF color) {
  bool ok = true;
  if (hdc == nullptr) {
    return false;
  }
  HBRUSH hBrush = CreateSolidBrush(color);
  if (hBrush == nullptr) {
    return false;
  }
  if (!FillRect(hdc, &rc, hBrush)) {
    ok = false;
  }
  DeleteObject(hBrush);
  return ok;
}

// Fills a convex polygon with a solid color. Pen and brush share the color
// so the rasterized outline doesn't leave a 1px halo around the fill.
void FillPolygon(HDC hdc, const POINT* pts, int count, COLORREF color) {
  HBRUSH hbr     = CreateSolidBrush(color);
  HPEN hpen      = CreatePen(PS_SOLID, 1, color);
  HGDIOBJ oldbr  = SelectObject(hdc, hbr);
  HGDIOBJ oldpen = SelectObject(hdc, hpen);
  Polygon(hdc, pts, count);
  SelectObject(hdc, oldbr);
  SelectObject(hdc, oldpen);
  DeleteObject(hbr);
  DeleteObject(hpen);
}

void FillRectWithGradient(HDC hdc,
                          const RECT& rc,
                          COLORREF topColor,
                          COLORREF bottomColor) {
  if (hdc == nullptr) {
    return;
  }
  const int height = rc.bottom - rc.top;
  if (height <= 0 || rc.right <= rc.left) {
    return;
  }
  const int r1 = GetRValue(topColor);
  const int g1 = GetGValue(topColor);
  const int b1 = GetBValue(topColor);
  const int r2 = GetRValue(bottomColor);
  const int g2 = GetGValue(bottomColor);
  const int b2 = GetBValue(bottomColor);
  // One filled row per scan line. Denominator is (height - 1) so the
  // very last row lands exactly on bottomColor instead of one step
  // shy of it.
  const double inv_span = (height > 1) ? 1.0 / (height - 1) : 0.0;
  for (int row_y = rc.top; row_y < rc.bottom; ++row_y) {
    const double frac = (row_y - rc.top) * inv_span;
    const int red   = static_cast<int>(std::lround(r1 + (r2 - r1) * frac));
    const int green = static_cast<int>(std::lround(g1 + (g2 - g1) * frac));
    const int blue  = static_cast<int>(std::lround(b1 + (b2 - b1) * frac));
    HBRUSH hBr = CreateSolidBrush(RGB(red, green, blue));
    if (hBr == nullptr) {
      continue;
    }
    RECT row = {rc.left, row_y, rc.right, row_y + 1};
    FillRect(hdc, &row, hBr);
    DeleteObject(hBr);
  }
}

const std::wstring GetExeDir() {
  wchar_t exe_path[MAX_PATH];
  HMODULE this_app = GetModuleHandleW(nullptr);
  if (!this_app) {
    return std::wstring();
  }
  DWORD got_path = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  if (got_path == 0 || got_path >= MAX_PATH) {
    return std::wstring();
  }

  // Find the last backslash to get the directory
  std::wstring fullPath(exe_path);
  size_t lastSlash = fullPath.find_last_of(L"\\/");
  std::wstring retval;
  if (lastSlash != std::wstring::npos) {
    retval = fullPath.substr(0, lastSlash + 1); // Include trailing slash
  } else {
    retval = fullPath;
  }
  return retval;
}

// MessageBoxW with MB_OK can be dismissed several ways the user considers
// equivalent: clicking OK (IDOK), clicking the X close button (IDCANCEL),
// or pressing Esc (IDCANCEL). All of those mean "the box showed and the
// user dismissed it" - which is what these helpers want to report as
// success. Only a 0 return means the box failed to display in the first
// place (bad hWnd, OOM, no desktop access, etc.); that's the real false.
// `hWnd ? hWnd : mainHwnd` falls back to the main window when the caller
// passed null - useful from helpers that don't have an hWnd of their own.
bool InfoBox(HWND hWnd, const std::wstring& title, const std::wstring& message) {
  return MessageBoxW(hWnd ? hWnd : mainHwnd, message.c_str(), title.c_str(),
                     MB_OK | MB_ICONINFORMATION) != 0;
}

bool WarnBox(HWND hWnd, const std::wstring& title, const std::wstring& message) {
  return MessageBoxW(hWnd ? hWnd : mainHwnd, message.c_str(), title.c_str(),
                     MB_OK | MB_ICONWARNING) != 0;
}

bool ErrorBox(HWND hWnd, const std::wstring& title, const std::wstring& message) {
  return MessageBoxW(hWnd ? hWnd : mainHwnd, message.c_str(), title.c_str(),
                     MB_OK | MB_ICONERROR) != 0;
}

const std::wstring GetVersionString() {
  // VERSION_STRING is a narrow C string literal built by stringize macros,
  // so we can't feed it straight to std::wstring. Build the wide form
  // directly from the same integer macros (single source of truth in
  // version.h) - std::to_wstring keeps it standards-clean across MinGW
  // and MSVC alike.
  return std::to_wstring(MAJOR_VERSION) + L"." + std::to_wstring(MINOR_VERSION) + L"." +
         std::to_wstring(BUILD_VERSION);
}

const std::wstring GetAppName() {
  const std::wstring app_name = std::wstring(APP_NAME);
  return app_name;
}

static bool GetRawNtVersion(UINT* major, UINT* minor, UINT* build) {
  HMODULE hNtDll = GetModuleHandleW(L"ntdll.dll");
  if (hNtDll == nullptr) {
    return false;
  }
  const RtlGetNtVersionNumbers_t pfnRtlGetNtVersionNumbers =
      reinterpret_cast<RtlGetNtVersionNumbers_t>(GetProcAddress(hNtDll, "RtlGetNtVersionNumbers"));
  if (pfnRtlGetNtVersionNumbers == nullptr) {
    return false;
  }
  DWORD majorVer = 0;
  DWORD minorVer = 0;
  DWORD buildVer = 0;
  pfnRtlGetNtVersionNumbers(&majorVer, &minorVer, &buildVer);
  if (majorVer == 0) {
    return false; // Should never be zero
  }
  // RtlGetNtVersionNumbers packs the build-type flag into the top 4 bits
  // of the build number: 0xC0000000 = checked (debug) build, 0xF0000000 =
  // free (release) build. Bit Mask them off so callers see the same plain
  // build number the OS reports everywhere else (e.g. 2600 on XP SP3,
  // 7601 on Win7 SP1, 19045 on a recent Win10) instead of the raw
  // 0xF0000A28 = 4026534440 mess.
  const DWORD cleanBuildVer = buildVer & 0x0FFFFFFFu;
  // Out-params are individually optional - skip the assignment if a caller
  // passed nullptr (e.g. they only care about the major version).
  if (major != nullptr) {
    *major = static_cast<unsigned int>(majorVer);
  }
  if (minor != nullptr) {
    *minor = static_cast<unsigned int>(minorVer);
  }
  if (build != nullptr) {
    *build = static_cast<unsigned int>(cleanBuildVer);
  }
  return true;
}

bool IsWindowsXpOrLater() {
  UINT major = 0;
  UINT minor = 0;
  // Use the raw NT version: can't be spoofed by the manifest-driven shim that
  // GetVersionExW / RtlGetVersion go through, anything higher than 5.0 returns true.
  if (GetRawNtVersion(&major, &minor, nullptr)) {
    return major > 5u || (major == 5u && minor >= 1u);
  }
  return false; // Safe fallback, assume Win 2K
}

static DWORD GetCommCtrlVersion() {
  static const wchar_t* kComCtl32Dll = L"comctl32.dll";
  // Resolve the system comctl32.dll path explicitly. GetSystemDirectoryW
  // returns 0 on failure, or >= MAX_PATH if our buffer was too small (in
  // which case it reports the required size). Either is fatal for us -
  // bail rather than fall through with an empty path that would let
  // LoadLibraryW search the standard DLL order and silently bypass the
  // "explicitly use the system one" intent.
  wchar_t systemDir[MAX_PATH];
  const UINT length = GetSystemDirectoryW(systemDir, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return 0x0;
  }
  const std::wstring comctl32_path = std::wstring(systemDir) + L"\\" + kComCtl32Dll;

  HMODULE hComCtl32Dll = LoadLibraryW(comctl32_path.c_str());
  if (hComCtl32Dll == nullptr) {
    return 0x0;
  }

  DWORD dwVersion = 0x0;
  DLLGETVERSIONPROC pDllGetVersion =
      reinterpret_cast<DLLGETVERSIONPROC>(GetProcAddress(hComCtl32Dll, "DllGetVersion"));
  if (pDllGetVersion == nullptr) {
    return 0x0;
  } else {
    DLLVERSIONINFO dvi = {sizeof(dvi)};
    const HRESULT hr   = pDllGetVersion(&dvi);
    if (hr == S_OK) {
      dwVersion = _PACKVERSION(dvi.dwMajorVersion, dvi.dwMinorVersion);
    }
  }
  FreeLibrary(hComCtl32Dll);
  return dwVersion;
}

bool IsCommCtrlAtLeast(const DWORD to_compare) {
  const DWORD kCommCtrlVer = GetCommCtrlVersion();
  return kCommCtrlVer >= to_compare;
}
