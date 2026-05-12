/*------------------------------------------
   Mother's Day Win32
   Copyright (c) 2026 Alex313031
  ------------------------------------------*/

#include "main.h"

#include "game.h"
#include "globals.h"
#include "resource.h"
#include "utils.h"

HWND mainHwnd = nullptr;

HINSTANCE g_hInstance = nullptr;

int cxClient = 0;
int cyClient = 0;

// Tracks whether the last WM_SIZE minimized the window. Set by WM_SIZE on
// SIZE_MINIMIZED, cleared by the next non-minimize WM_SIZE. Used to decide
// whether the just-arrived size event is "we're coming back from a
// minimize" (restart the tick source) vs. a normal resize (no-op).
static bool s_was_minimized = false;

// Current background color. Defaults to blue;
COLORREF g_bkg_color = RGB_BLACK;

bool g_debug_mode = is_debug;

// Store handles to main icon since commonly used
HICON kMainIcon  = nullptr;
HICON kSmallIcon = nullptr;

// Whether we have commctl32 5.82 (XP/I.E 6.0)
bool can_use_582_controls = false;

bool RegisterWndClass(HINSTANCE hInstance, LPCWSTR className) {
  if (kMainIcon == nullptr || kSmallIcon == nullptr) {
    return false;
  }
  WNDCLASSEXW wndclass;
  wndclass.cbSize      = sizeof(WNDCLASSEX);
  wndclass.style       = CS_HREDRAW | CS_VREDRAW;
  wndclass.lpfnWndProc = WindowProc;
  wndclass.cbClsExtra  = 0;
  wndclass.cbWndExtra  = 0;
  wndclass.hInstance   = hInstance;
  wndclass.hIcon       = kMainIcon;
  wndclass.hCursor     = LoadCursorW(nullptr, IDC_ARROW);
  // We handle erase + paint ourselves - WM_ERASEBKGND
  // returns TRUE and WM_PAINT fills with g_bkg_color. nullptr here skips
  // the OS's pre-fill entirely so there's no flash of a wrong-colored
  // window before our first WM_PAINT.
  wndclass.hbrBackground = nullptr;
  wndclass.lpszMenuName  = MAKEINTRESOURCEW(IDR_MAIN);
  wndclass.lpszClassName = className;
  wndclass.hIconSm       = kSmallIcon;

  // RegisterClassEx returns an ATOM (typedef unsigned short - really a short
  // pointer left over from Win16 days), 0 on failure. The double cast spells
  // out "this is an ATOM-shaped zero" rather than relying on the implicit
  // promotion from int 0.
  if (RegisterClassExW(&wndclass) == static_cast<ATOM>(static_cast<unsigned short>(0))) {
    return false;
  }
  return true;
}

bool InitWindow(HINSTANCE hInstance, LPCWSTR className, LPCWSTR title, int iCmdShow) {
  static constexpr DWORD exStyle = WS_EX_OVERLAPPEDWINDOW;
  static constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                                 WS_MAXIMIZEBOX | WS_SIZEBOX;

  // Create main window
  mainHwnd = CreateWindowExW(exStyle, className, title, style, CW_USEDEFAULT, CW_USEDEFAULT,
                             CW_WIDTH, CW_HEIGHT, nullptr, nullptr, hInstance, nullptr);

  if (mainHwnd == nullptr) {
    return false;
  }
  ShowWindow(mainHwnd, iCmdShow);
  if (!UpdateWindow(mainHwnd)) {
    return false;
  }
  return true;
}

int APIENTRY wWinMain(HINSTANCE hInstance,
                      HINSTANCE hPrevInstance,
                      LPWSTR lpCmdLine,
                      int iCmdShow) {
  UNREFERENCED_PARAMETER(hPrevInstance);
  g_hInstance = hInstance;
  // Initialize common controls
  INITCOMMONCONTROLSEX icex;
  icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
  icex.dwICC  = ICC_STANDARD_CLASSES;
  InitCommonControlsEx(&icex);
  // Now that comctl32 is initialized, probe its version once for callers
  // that need to gate v5.82+ behavior (notably the status-bar tooltip
  // TOOLINFO size that Win2000's v5.81 doesn't accept).
  can_use_582_controls = IsCommCtrlAtLeast(dwComCtl32TargetVer);

  static const std::wstring name   = GetAppName();
  static const LPCWSTR appTitle    = name.c_str();
  static const LPCWSTR szClassName = MAIN_WNDCLASS;

  kMainIcon  = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_MAIN));
  kSmallIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_SMALL));

  // Register our window class.
  if (!RegisterWndClass(g_hInstance, szClassName)) {
    ErrorBox(nullptr, L"RegisterClassEx Error", L"This program requires Windows NT!");
    return 1;
  }

  // Open our window now
  if (!InitWindow(g_hInstance, szClassName, appTitle, iCmdShow)) {
    return 4;
  }

  HACCEL hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDR_MAIN));
  if (hAccel == nullptr) {
    return 5;
  }

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    if (!TranslateAcceleratorW(mainHwnd, hAccel, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  if (hAccel != nullptr) {
    DestroyAcceleratorTable(hAccel);
  }
  return static_cast<int>(msg.wParam);
}

// Middle-drag-to-resize state. We can't use the WM_NCLBUTTONDOWN trick
// (which drops the OS into its modal sizing loop) because that loop
// only exits on a *left* mouse-up. Instead we capture the mouse on
// WM_MBUTTONDOWN, anchor the opposite corner, and drive SetWindowPos
// directly from WM_MOUSEMOVE until WM_MBUTTONUP / WM_CAPTURECHANGED.
// (Right-click is left free for a future popup menu.)
static bool s_resizing                 = false;
static POINT s_resize_start_screen     = {0, 0};
static RECT s_resize_start_window      = {0, 0, 0, 0};
static WPARAM s_resize_corner          = HTBOTTOMRIGHT;
// Smallest window we'll let the right-drag resize produce. Mirrors the
// floor in WM_GETMINMAXINFO so manual dragging can't undercut it.
constexpr int kMinResizeWindowSide     = 200;

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE:
      if (mainHwnd == nullptr) {
        mainHwnd = hWnd; // Prevent race condition in InitApp
      }
      InitApp(hWnd);
      break;
    case WM_TIMER:
      break;
    case WM_RBUTTONDOWN: {
      // Right-click pops up the Game submenu at the cursor. We grab
      // the live menu off the window (vs LoadMenu / a fresh handle)
      // so any future runtime menu state - greys, checks, etc. - is
      // mirrored. TrackPopupMenu's selections come back as WM_COMMAND
      // with the same IDs as the menu bar, so IDM_REPAINT / IDM_EXIT
      // re-use their existing handlers automatically.
      HMENU hMenuBar = GetMenu(hWnd);
      if (hMenuBar != nullptr) {
        HMENU hFileMenu = GetSubMenu(hMenuBar, 1);
        if (hFileMenu != nullptr) {
          POINT screenPt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
          ClientToScreen(hWnd, &screenPt);
          TrackPopupMenu(hFileMenu, TPM_RIGHTBUTTON | TPM_LEFTALIGN,
                         screenPt.x, screenPt.y, 0, hWnd, nullptr);
        }
      }
      break;
    }
    case WM_MBUTTONDOWN: {
      // Start a middle-drag resize: pick the corner nearest the cursor
      // (so the opposite corner stays anchored), snapshot the cursor
      // and window in screen coords, take the mouse capture so we
      // keep getting MOUSEMOVE messages even when the cursor leaves
      // the client area, and flip s_resizing on. WM_MOUSEMOVE does
      // the actual resize, WM_MBUTTONUP / WM_CAPTURECHANGED end it.
      const int px = GET_X_LPARAM(lParam);
      const int py = GET_Y_LPARAM(lParam);
      RECT client;
      GetClientRect(hWnd, &client);
      const int midX = (client.left + client.right) / 2;
      const int midY = (client.top + client.bottom) / 2;
      if (px < midX && py < midY) {
        s_resize_corner = HTTOPLEFT;
      } else if (px >= midX && py < midY) {
        s_resize_corner = HTTOPRIGHT;
      } else if (px < midX && py >= midY) {
        s_resize_corner = HTBOTTOMLEFT;
      } else {
        s_resize_corner = HTBOTTOMRIGHT;
      }
      GetCursorPos(&s_resize_start_screen);
      GetWindowRect(hWnd, &s_resize_start_window);
      SetCapture(hWnd);
      s_resizing = true;
      break;
    }
    case WM_MOUSEMOVE: {
      if (s_resizing) {
        // Compute the new window rect by moving only the dragged
        // corner's two edges by the screen-space cursor delta. Then
        // clamp width/height against the minimum, anchoring the
        // *opposite* edge so the anchor side doesn't drift when we
        // bottom out.
        POINT cur;
        GetCursorPos(&cur);
        const int dx = cur.x - s_resize_start_screen.x;
        const int dy = cur.y - s_resize_start_screen.y;
        RECT r = s_resize_start_window;
        switch (s_resize_corner) {
          case HTTOPLEFT:
            r.left += dx;
            r.top += dy;
            break;
          case HTTOPRIGHT:
            r.right += dx;
            r.top += dy;
            break;
          case HTBOTTOMLEFT:
            r.left += dx;
            r.bottom += dy;
            break;
          case HTBOTTOMRIGHT:
          default:
            r.right += dx;
            r.bottom += dy;
            break;
        }
        if (r.right - r.left < kMinResizeWindowSide) {
          if (s_resize_corner == HTTOPLEFT || s_resize_corner == HTBOTTOMLEFT) {
            r.left = r.right - kMinResizeWindowSide;
          } else {
            r.right = r.left + kMinResizeWindowSide;
          }
        }
        if (r.bottom - r.top < kMinResizeWindowSide) {
          if (s_resize_corner == HTTOPLEFT || s_resize_corner == HTTOPRIGHT) {
            r.top = r.bottom - kMinResizeWindowSide;
          } else {
            r.bottom = r.top + kMinResizeWindowSide;
          }
        }
        SetWindowPos(hWnd, nullptr, r.left, r.top,
                     r.right - r.left, r.bottom - r.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      break;
    }
    case WM_MBUTTONUP:
      if (s_resizing) {
        ReleaseCapture();  // triggers WM_CAPTURECHANGED, which clears s_resizing
      }
      break;
    case WM_CAPTURECHANGED:
      // Fired when capture is released for any reason (our own
      // ReleaseCapture, an alt-tab, another window stealing it, etc.).
      // Clearing here is the single point of truth for ending a drag.
      s_resizing = false;
      break;
    case WM_APP_AUTOPLAY:
      break;
    case WM_ERASEBKGND:
      // Returning TRUE tells Windows we have handled background erasing
      // ourselves, suppressing the default white fill. We do our own filling
      // in WM_PAINT so the two operations don't race or double-paint.
      return TRUE;
    case WM_GETMINMAXINFO: {
      LPMINMAXINFO pMinMaxInfo = reinterpret_cast<LPMINMAXINFO>(lParam);;
      pMinMaxInfo->ptMinTrackSize.x = CW_MINWIDTH;
      pMinMaxInfo->ptMinTrackSize.y = CW_MINHEIGHT;
      const int MAXWIDTH            = GetSystemMetrics(SM_CXMAXIMIZED);
      const int MAXHEIGHT           = GetSystemMetrics(SM_CYMAXIMIZED);
      pMinMaxInfo->ptMaxTrackSize.x = MAXWIDTH;
      pMinMaxInfo->ptMaxTrackSize.y = MAXHEIGHT;
      break;
    }
    case WM_PAINT: {
      // WM_ERASEBKGND returned TRUE so Windows skipped its bg fill - we
      // paint a vertical RGB_BLUE -> RGB_DARKBLUE gradient over the
      // entire client rect (not just ps.rcPaint - we need the full
      // client height so the gradient interpolation references the
      // window, not the dirty region; BeginPaint's clip handles the
      // rest). Then layer the heart on top via DrawHeart (its own DC).
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      RECT client;
      GetClientRect(hWnd, &client);
      EndPaint(hWnd, &ps);
      break;
    }
    case WM_SIZE: {
      // cxClient / cyClient represent the games canvas area, not the parent's
      // client area.
      cxClient = LOWORD(lParam);
      cyClient = HIWORD(lParam);
      if (wParam == SIZE_MINIMIZED) {
        s_was_minimized = true;
        break;
      }
      if (cyClient < 0) {
        cyClient = 0;
      }
      break;
    }
    case WM_COMMAND: {
      const int command = LOWORD(wParam);
      switch (command) {
        case IDM_EXIT:
          ShutDownApp();
          break;
        case IDM_ABOUT:
          PlaySoundW(L"SystemNotification", nullptr, SND_ALIAS | SND_ASYNC);
          DialogBoxW(g_hInstance, MAKEINTRESOURCEW(IDD_ABOUTDLG), hWnd, AboutDlgProc);
          break;
        case IDM_HELP:
          LaunchHelp(hWnd);
          break;
        case IDM_SAVEAS: {
          std::wstring savepath;
          if (!SaveClientBitmap(hWnd, &savepath)) {
            ErrorBox(hWnd, L"Save Screenshot Error", L"Failed to save screenshot!");
          }
          break;
        }
        default:
          return DefWindowProcW(hWnd, message, wParam, lParam);
      }
    } break;
    case WM_HELP:
      LaunchHelp(hWnd);
      break;
    case WM_CLOSE:
      ShutDownApp();
      break;
    case WM_QUERYENDSESSION:
      return TRUE;
    case WM_DESTROY:
      // If the window is dying mid middle-drag-resize (e.g. Alt+F4
      // while MMB is held), release capture explicitly. The OS would
      // do it anyway when the window goes away, but being explicit
      // means s_resizing stays in a consistent state.
      if (s_resizing) {
        ReleaseCapture();
        s_resizing = false;
      }
      KillTimer(hWnd, TIMER_GAME);
      PostQuitMessage(0);
      break;
    case WM_NCDESTROY:
      mainHwnd = nullptr;
      break;
    default:
      return DefWindowProcW(hWnd, message, wParam, lParam);
  }
  return 0;
}

bool InitApp(HWND hWnd) {
  if (hWnd == nullptr) {
    return false;
  }
  // ~33 fps tick rate.
  if (SetTimer(hWnd, TIMER_GAME, 30, nullptr) == 0) {
    return false;
  }
  return true;
}

void ShutDownApp() {
  // mainHwnd is cleared in WM_NCDESTROY; guard so a duplicate exit
  // path (e.g. WM_CLOSE arriving after WM_DESTROY's tear-down began)
  // doesn't pass NULL to DestroyWindow, which is undefined per MSDN.
  if (mainHwnd != nullptr) {
    DestroyWindow(mainHwnd);
  }
}

bool LaunchHelp(HWND hWnd) {
  bool success = false;
  if (InfoBox(hWnd, L"Help32", L"No help yet...")) {
    success = true;
  }
  return success;
}

INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
  UNREFERENCED_PARAMETER(lParam);
  switch (message) {
    case WM_INITDIALOG:
      // Set icon in titlebar of about dialog
      static const HICON kAboutIcon = LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_ABOUT));
      SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)kAboutIcon);
      SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)kAboutIcon);
      return TRUE;
    case WM_CLOSE:
      EndDialog(hDlg, TRUE);
      return TRUE;
    case WM_COMMAND:
      if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
        EndDialog(hDlg, LOWORD(wParam));
        return TRUE;
      }
      break;
    default:
      break;
  }
  return FALSE;
}
