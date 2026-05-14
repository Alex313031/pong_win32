/*------------------------------------------
   Pong Win32
   Copyright (c) 2026 Alex313031
  ------------------------------------------*/

#include "main.h"

#include "game.h"
#include "globals.h"
#include "resource.h"
#include "sound.h"
#include "strings.h"
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

// True until the user has first started a game (via F3 / Space, F2, or
// the welcome auto-start). While true, any arrow-key press from the
// welcome screen kicks the game off without needing F3. Once the user
// has interacted in any way that exits the welcome screen, this stays
// false for the rest of the session - new games and pauses always
// require F3 / Space to resume.
static bool s_first_start = true;

// Background colours. g_bkg_color is the solid-fill for the playfield and
// the bottom of any vertical gradient; g_top_color is the top of a vertical
// gradient (currently the message-area backdrop, available for a future
// full-canvas gradient too). Defaults match the message area's classic
// "dark grey fading to black" look.
COLORREF g_bkg_color = RGB_BLACK;
COLORREF g_top_color = RGB_DKGREY;

// Match-level run state. false = game stopped (between matches, or on first
// launch sitting at the "ready" screen). The tick handlers in game.cc gate
// movement on both this and g_paused so the ball doesn't drift while a
// kReady / kNewGame banner is up. volatile in case future work moves the
// tick / input loop onto another thread.
volatile bool g_running = false;

// Back-buffer for double-buffered painting. All Draw* calls in WM_PAINT
// target g_hdcMem; we BitBlt the dirty rect to the window DC at the end
// of the handler. The back buffer is also what SaveClientBitmap clones to
// freeze the exact frame the user saw when they clicked the menu item.
//
// Declared extern in utils.h so SaveClientBitmap (in utils.cc) can read
// from it without having to thread a parameter through every call site.
HDC g_hdcMem            = nullptr;
HBITMAP g_hbmMem        = nullptr;
static HBITMAP s_hbm_default = nullptr; // default 1x1 bitmap, restored on destroy
static int s_back_w     = 0;
static int s_back_h     = 0;

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
  // We handle erase + paint ourselves - WM_ERASEBKGND returns
  // TRUE and WM_PAINT fills with g_bkg_color. Set to black default as fallback.
  wndclass.hbrBackground = CreateSolidBrush(g_bkg_color);
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

// Menu-state helpers. The .rc's CHECKED flags double as default-setting
// storage - ApplyMenuDefaults reads each item's initial state at startup
// and pushes it into the engine, so adjusting the defaults is just a
// matter of toggling CHECKED in the .rc.
static bool IsMenuChecked(HMENU menu, UINT id) {
  if (menu == nullptr) {
    return false;
  }
  const UINT state = GetMenuState(menu, id, MF_BYCOMMAND);
  // GetMenuState returns 0xFFFFFFFF when the item isn't found.
  if (state == static_cast<UINT>(-1)) {
    return false;
  }
  return (state & MF_CHECKED) != 0;
}

// Flips a checkable menu item and returns the new state. Used by the
// WM_COMMAND handlers so a single line covers "toggle + push into engine".
static bool ToggleMenuCheck(HWND hWnd, UINT id) {
  HMENU menu = GetMenu(hWnd);
  if (menu == nullptr) {
    return false;
  }
  const bool now_checked = !IsMenuChecked(menu, id);
  CheckMenuItem(menu, id,
                MF_BYCOMMAND | (now_checked ? MF_CHECKED : MF_UNCHECKED));
  return now_checked;
}

// Maps a Speed enum to the matching menu command ID. Used in both the
// .rc default load path and the WM_COMMAND handlers to keep the radio
// item in sync with the underlying state.
static UINT SpeedMenuId(Speed speed) {
  switch (speed) {
    case Speed::Low:  return IDM_SPEED_LOW;
    case Speed::High: return IDM_SPEED_HIGH;
    case Speed::Med:
    default:          return IDM_SPEED_MED;
  }
}

static UINT DifficultyMenuId(Difficulty difficulty) {
  switch (difficulty) {
    case Difficulty::Easy: return IDM_EASY;
    case Difficulty::Hard: return IDM_HARD;
    case Difficulty::Med:
    default:               return IDM_MED;
  }
}

// Applies a Speed selection: pushes it into game.cc and flips the radio
// check on the matching menu item (which also un-checks the other two).
static void ApplySpeedSelection(HWND hWnd, Speed speed) {
  SetSpeed(speed);
  HMENU menu = GetMenu(hWnd);
  if (menu != nullptr) {
    CheckMenuRadioItem(menu, IDM_SPEED_LOW, IDM_SPEED_HIGH,
                       SpeedMenuId(speed), MF_BYCOMMAND);
  }
}

static void ApplyDifficultySelection(HWND hWnd, Difficulty difficulty) {
  SetDifficulty(difficulty);
  HMENU menu = GetMenu(hWnd);
  if (menu != nullptr) {
    CheckMenuRadioItem(menu, IDM_EASY, IDM_HARD,
                       DifficultyMenuId(difficulty), MF_BYCOMMAND);
  }
}

// Reads every menu item whose CHECKED state is wired to a runtime setting
// and applies it before the first frame. Add new ID->setter pairs here as
// settings come online.
static void ApplyMenuDefaults(HWND hWnd) {
  HMENU menu = GetMenu(hWnd);
  if (menu == nullptr) {
    return;
  }
  SetPaused(IsMenuChecked(menu, IDM_PAUSE));
  SetPlayerOnLeft(IsMenuChecked(menu, IDM_PLAYER));
  SetSoundOn(IsMenuChecked(menu, IDM_SOUND));
  // Speed / difficulty are radio groups. If no item in a group has CHECKED
  // set in the .rc, default to Med. ApplySpeedSelection/...Difficulty also
  // refresh the radio check so the menu visually matches the runtime state.
  Speed speed = Speed::Med;
  if (IsMenuChecked(menu, IDM_SPEED_LOW)) {
    speed = Speed::Low;
  } else if (IsMenuChecked(menu, IDM_SPEED_HIGH)) {
    speed = Speed::High;
  }
  ApplySpeedSelection(hWnd, speed);
  Difficulty difficulty = Difficulty::Med;
  if (IsMenuChecked(menu, IDM_EASY)) {
    difficulty = Difficulty::Easy;
  } else if (IsMenuChecked(menu, IDM_HARD)) {
    difficulty = Difficulty::Hard;
  }
  ApplyDifficultySelection(hWnd, difficulty);
}

// Drives the Pause menu's CHECKED state from the actual run state. We treat
// "ball isn't moving right now" as one bit, regardless of whether that's
// because the match is stopped (between rounds) or actively paused inside
// one - so the user always has a single visual indicator that says "play
// is halted". Called from every site that flips g_running or g_paused.
static void SyncPauseMenuCheck(HWND hWnd) {
  HMENU menu = GetMenu(hWnd);
  if (menu == nullptr) {
    return;
  }
  const bool checked = !g_running || g_paused;
  CheckMenuItem(menu, IDM_PAUSE,
                MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
}

// Welcome-screen convenience: while we're still on the very first
// stopped-state (no F3 / F2 yet), an arrow key kicks the game off the
// same way F3 would. After any state change leaves the welcome screen
// (s_first_start gets cleared), this is a no-op forever - new games
// and pauses always need F3 / Space to resume per the user's spec.
// Called from WM_TIMER before the tick handlers so the same arrow press
// that starts the game also feeds into TickPlayerRacket this frame.
static void CheckWelcomeAutoStart(HWND hWnd) {
  if (!s_first_start || g_running) {
    return;
  }
  // Same foreground guard as TickPlayerRacket - we read keyboard state
  // globally, so a key held in another app wouldn't be "us".
  if (GetForegroundWindow() != hWnd) {
    return;
  }
  const bool any_arrow =
      (GetAsyncKeyState(VK_UP)    & 0x8000) ||
      (GetAsyncKeyState(VK_DOWN)  & 0x8000) ||
      (GetAsyncKeyState(VK_LEFT)  & 0x8000) ||
      (GetAsyncKeyState(VK_RIGHT) & 0x8000);
  if (!any_arrow) {
    return;
  }
  // Mirror what IDM_PAUSE does on the stopped->running transition:
  // flip g_running, clear the banner, refresh the menu indicator. We
  // additionally retire the welcome state so subsequent stops require
  // an explicit F3.
  g_running     = true;
  s_first_start = false;
  SetMessage(std::wstring());
  SyncPauseMenuCheck(hWnd);
}

// Mouse-drag window manipulation:
//   * Left-click drag in the client area  -> move the window.
//   * Right-click drag in the client area -> resize from the nearest corner.
//   * Middle click                        -> nothing.
// We can't use the WM_NCLBUTTONDOWN/HTCAPTION trick for either because the
// OS's modal move/size loop only exits on a *left* mouse-up - it'd work
// for moves but resize uses the right button, so for consistency we drive
// both manually: capture the mouse on button-down, snapshot the starting
// window rect + cursor in screen coords, and update via SetWindowPos in
// WM_MOUSEMOVE until the matching button-up or WM_CAPTURECHANGED.
static bool s_moving               = false;
static bool s_resizing             = false;
static POINT s_drag_start_cursor   = {0, 0};
static RECT s_drag_start_window    = {0, 0, 0, 0};
static WPARAM s_resize_corner      = HTBOTTOMRIGHT;
// Smallest window we'll let the right-drag resize produce. Mirrors the
// floor in WM_GETMINMAXINFO so manual dragging can't undercut it.
constexpr int kMinResizeWindowSide = 200;

// Back-buffer lifecycle helpers. EnsureBackBuffer is called from WM_PAINT
// each frame - cheap no-op when the cached size matches the client.
// DestroyBackBuffer runs in WM_DESTROY. Compatible bitmaps come with a
// default 1x1 bitmap selected; we save it to s_hbm_default and restore it
// before DeleteDC so GDI doesn't leak the bitmap we selected in.
static bool EnsureBackBuffer(HDC hdc_ref, int width, int height) {
  if (g_hdcMem != nullptr && g_hbmMem != nullptr &&
      s_back_w == width && s_back_h == height) {
    return true;
  }
  // Recreate from scratch when size changes or first call.
  if (g_hdcMem != nullptr) {
    SelectObject(g_hdcMem, s_hbm_default);
    DeleteDC(g_hdcMem);
    g_hdcMem = nullptr;
  }
  if (g_hbmMem != nullptr) {
    DeleteObject(g_hbmMem);
    g_hbmMem = nullptr;
  }
  g_hdcMem = CreateCompatibleDC(hdc_ref);
  if (g_hdcMem == nullptr) {
    return false;
  }
  g_hbmMem = CreateCompatibleBitmap(hdc_ref, width, height);
  if (g_hbmMem == nullptr) {
    DeleteDC(g_hdcMem);
    g_hdcMem = nullptr;
    return false;
  }
  s_hbm_default = static_cast<HBITMAP>(SelectObject(g_hdcMem, g_hbmMem));
  s_back_w      = width;
  s_back_h      = height;
  return true;
}

static void DestroyBackBuffer() {
  if (g_hdcMem != nullptr) {
    if (s_hbm_default != nullptr) {
      SelectObject(g_hdcMem, s_hbm_default);
      s_hbm_default = nullptr;
    }
    DeleteDC(g_hdcMem);
    g_hdcMem = nullptr;
  }
  if (g_hbmMem != nullptr) {
    DeleteObject(g_hbmMem);
    g_hbmMem = nullptr;
  }
  s_back_w = 0;
  s_back_h = 0;
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_CREATE:
      if (mainHwnd == nullptr) {
        mainHwnd = hWnd; // Prevent race condition in InitApp
      }
      InitApp(hWnd);
      break;
    case WM_TIMER: {
      // Measure real elapsed time once per frame and pass it to both tick
      // functions so movement is timer-rate independent - the WM_TIMER
      // cadence can stall under OS load without warping the ball's speed.
      const float dt = NextFrameDelta();
      // Welcome-screen arrow-key auto-start. Must run before TickRackets
      // so the same key press that flips g_running also drives the
      // player's racket on this very frame.
      CheckWelcomeAutoStart(hWnd);
      TickRackets(hWnd, dt);
      TickBall(hWnd, dt);
      break;
    }
    case WM_LBUTTONDOWN: {
      // Start a click-and-drag window move. Snapshot cursor + window in
      // screen coords, take the mouse capture so MOUSEMOVE keeps arriving
      // when the cursor leaves the client area, and flip s_moving on.
      // WM_MOUSEMOVE drives the actual reposition; WM_LBUTTONUP /
      // WM_CAPTURECHANGED ends it. Guard against starting a second drag
      // while a right-drag resize is in progress.
      if (s_resizing) {
        break;
      }
      GetCursorPos(&s_drag_start_cursor);
      GetWindowRect(hWnd, &s_drag_start_window);
      SetCapture(hWnd);
      s_moving = true;
      break;
    }
    case WM_RBUTTONDOWN: {
      // Start a right-drag resize: pick the corner nearest the cursor
      // (so the opposite corner stays anchored), snapshot the cursor and
      // window in screen coords, take the mouse capture, and flip
      // s_resizing on. Guard against starting while a left-drag move is
      // in progress.
      if (s_moving) {
        break;
      }
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
      GetCursorPos(&s_drag_start_cursor);
      GetWindowRect(hWnd, &s_drag_start_window);
      SetCapture(hWnd);
      s_resizing = true;
      break;
    }
    case WM_MOUSEMOVE: {
      if (s_moving) {
        // Translate the window by the screen-space cursor delta since the
        // drag started. SWP_NOSIZE keeps the current size; SWP_NOZORDER /
        // SWP_NOACTIVATE avoid unrelated side-effects.
        POINT cur;
        GetCursorPos(&cur);
        const int dx = cur.x - s_drag_start_cursor.x;
        const int dy = cur.y - s_drag_start_cursor.y;
        SetWindowPos(hWnd, nullptr,
                     s_drag_start_window.left + dx,
                     s_drag_start_window.top + dy,
                     0, 0,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
      }
      if (s_resizing) {
        // Compute the new window rect by moving only the dragged
        // corner's two edges by the screen-space cursor delta. Then
        // clamp width/height against the minimum, anchoring the
        // *opposite* edge so the anchor side doesn't drift when we
        // bottom out.
        POINT cur;
        GetCursorPos(&cur);
        const int dx = cur.x - s_drag_start_cursor.x;
        const int dy = cur.y - s_drag_start_cursor.y;
        RECT rect = s_drag_start_window;
        switch (s_resize_corner) {
          case HTTOPLEFT:
            rect.left += dx;
            rect.top += dy;
            break;
          case HTTOPRIGHT:
            rect.right += dx;
            rect.top += dy;
            break;
          case HTBOTTOMLEFT:
            rect.left += dx;
            rect.bottom += dy;
            break;
          case HTBOTTOMRIGHT:
          default:
            rect.right += dx;
            rect.bottom += dy;
            break;
        }
        if (rect.right - rect.left < kMinResizeWindowSide) {
          if (s_resize_corner == HTTOPLEFT || s_resize_corner == HTBOTTOMLEFT) {
            rect.left = rect.right - kMinResizeWindowSide;
          } else {
            rect.right = rect.left + kMinResizeWindowSide;
          }
        }
        if (rect.bottom - rect.top < kMinResizeWindowSide) {
          if (s_resize_corner == HTTOPLEFT || s_resize_corner == HTTOPRIGHT) {
            rect.top = rect.bottom - kMinResizeWindowSide;
          } else {
            rect.bottom = rect.top + kMinResizeWindowSide;
          }
        }
        SetWindowPos(hWnd, nullptr, rect.left, rect.top,
                     rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      break;
    }
    case WM_LBUTTONUP:
      if (s_moving) {
        ReleaseCapture();  // triggers WM_CAPTURECHANGED, which clears s_moving
      }
      break;
    case WM_RBUTTONUP:
      if (s_resizing) {
        ReleaseCapture();  // triggers WM_CAPTURECHANGED, which clears s_resizing
      }
      break;
    case WM_CAPTURECHANGED:
      // Fired when capture is released for any reason (our own
      // ReleaseCapture, an alt-tab, another window stealing it, etc.).
      // Single point of truth for ending either kind of drag.
      s_moving   = false;
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
      // Double-buffered paint. WM_ERASEBKGND returned TRUE so Windows
      // skipped its bg fill - we own the entire client rect. Every
      // Draw* call targets g_hdcMem (the back buffer); we BitBlt only
      // the dirty rect (ps.rcPaint) to the screen at the end. The full
      // back-buffer redraw on every WM_PAINT means we don't have to do
      // per-element dirty tracking inside the Draw* helpers, and the
      // user sees a single atomic frame instead of the intermediate
      // bg-fill / divider / center-line / displays / rackets / ball
      // states flickering past on screen.
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hWnd, &ps);
      RECT client;
      GetClientRect(hWnd, &client);
      const int width  = client.right - client.left;
      const int height = client.bottom - client.top;
      if (width > 0 && height > 0 && EnsureBackBuffer(hdc, width, height)) {
        FillRectWithColor(g_hdcMem, client, g_bkg_color);
        DrawSpawnCircle(g_hdcMem, client);
        DrawPlayfieldDivider(g_hdcMem, client);
        DrawMessageArea(g_hdcMem, client);
        DrawCenterLine(g_hdcMem, client);
        DrawSegmentDisplays(g_hdcMem, client);
        DrawRackets(g_hdcMem, client);
        DrawBall(g_hdcMem, client);
        BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top,
               ps.rcPaint.right  - ps.rcPaint.left,
               ps.rcPaint.bottom - ps.rcPaint.top,
               g_hdcMem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
      }
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
      // Mid-play the ball and paddles are at game-state-driven positions
      // and we don't want resizing to teleport them; but while the game is
      // stopped (between matches / on the ready screen) everything should
      // track the recentred playfield as the window changes shape.
      if (!g_running) {
        CenterBallAtSpawn();
        CenterRackets();
      }
      break;
    }
    case WM_COMMAND: {
      const int command = LOWORD(wParam);
      switch (command) {
        case IDM_CEXIT:
          if (ConfirmExit(hWnd)) {
            ShutDownApp();
          }
          break;
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
        case IDM_NEWGAME:
          if (ConfirmNewGame(hWnd)) {
            // Stop the game, reset positions / scores, and surface the new-
            // game banner. The ball is spawned with a random velocity but
            // won't actually move until g_running flips true on the next F3.
            // Pause flag also gets cleared so a previously-paused match
            // doesn't carry over into the fresh one. We also retire the
            // welcome-screen auto-start: a New Game banner needs an explicit
            // F3 to start play, never an arrow key.
            g_running     = false;
            s_first_start = false;
            SetPaused(false);
            ResetForNewGame(hWnd);
            SetMessage(kNewGameMsg);
          }
          SyncPauseMenuCheck(hWnd);
          break;
        case IDM_PAUSE:
          // Two roles for F3 / IDM_PAUSE depending on the run state:
          //   * Game stopped (!g_running): act as "start" - flip g_running
          //     on and clear the banner. g_paused is left alone since this
          //     transition is "stopped to playing", not a pause toggle.
          //   * Game running: standard pause toggle - flip g_paused.
          // Either way, SyncPauseMenuCheck folds the new state back into
          // the menu's CHECKED indicator.
          if (!g_running) {
            g_running     = true;
            s_first_start = false;
            SetMessage(std::wstring());
          } else {
            SetPaused(!g_paused);
            SetMessage(g_paused ? kPausedMsg : std::wstring());
          }
          SyncPauseMenuCheck(hWnd);
          break;
        case IDM_PLAYER:
          // CHECKED == player on left, unchecked == player on right. Full
          // client invalidate because both racket colours and the score
          // displays swap sides with g_player_on_left.
          SetPlayerOnLeft(ToggleMenuCheck(hWnd, IDM_PLAYER));
          SetMessage(kToggleMsg);
          InvalidateRect(hWnd, nullptr, FALSE);
          break;
        case IDM_SOUND: {
          // CHECKED == sound on. kUnMuteMsg is the "sound is now on"
          // banner, kMuteMsg the "sound is now off" one, so the message
          // matches the new state, not the previous one.
          const bool now_on = ToggleMenuCheck(hWnd, IDM_SOUND);
          SetSoundOn(now_on);
          SetMessage(now_on ? kUnMuteMsg : kMuteMsg);
          break;
        }
        // Speed group (radio). ApplySpeedSelection both updates the
        // engine and switches the radio dot to the chosen item.
        case IDM_SPEED_LOW:  ApplySpeedSelection(hWnd, Speed::Low);  break;
        case IDM_SPEED_MED:  ApplySpeedSelection(hWnd, Speed::Med);  break;
        case IDM_SPEED_HIGH: ApplySpeedSelection(hWnd, Speed::High); break;
        // Difficulty group (radio). Only nudges the CPU racket speed.
        case IDM_EASY: ApplyDifficultySelection(hWnd, Difficulty::Easy); break;
        case IDM_MED:  ApplyDifficultySelection(hWnd, Difficulty::Med);  break;
        case IDM_HARD: ApplyDifficultySelection(hWnd, Difficulty::Hard); break;
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
      // If the window is dying mid-drag (e.g. Alt+F4 while a mouse button
      // is held), release capture explicitly. The OS would do it anyway
      // when the window goes away, but being explicit keeps s_moving /
      // s_resizing in a consistent state.
      if (s_moving || s_resizing) {
        ReleaseCapture();
        s_moving   = false;
        s_resizing = false;
      }
      KillTimer(hWnd, TIMER_GAME);
      // Order matters: StopPlayWav has to land on a live worker so its
      // mci stop+close runs; ShutDownBgm then signals exit and joins.
      // After this returns the BGM worker thread is gone and its
      // hidden-notify window is destroyed.
      StopPlayWav();
      ShutDownBgm();
      // Release the double-buffer GDI handles. Done after the audio
      // shutdown above (audio doesn't need GDI; the order is just
      // "stop the noisy stuff first").
      DestroyBackBuffer();
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
  // Spin up the BGM worker BEFORE ApplyMenuDefaults - the latter calls
  // SetSoundOn (which routes through SyncBgm -> PostBgmSync), and
  // PostBgmSync silently no-ops if the worker isn't initialised.
  // InitBgm returning false isn't fatal; we just won't have music.
  InitBgm();
  // Pull defaults from the menu's CHECKED state first - InitRackets etc.
  // observe g_player_on_left and g_paused, so they need the final values
  // by the time they run.
  ApplyMenuDefaults(hWnd);
  // Startup message. If the .rc starts the game paused we surface that
  // explicitly; otherwise show the welcome string. Either way the user has
  // immediate visual feedback on game state without needing to act first.
  SetMessage(IsMenuChecked(GetMenu(hWnd), IDM_PAUSE) ? kPausedMsg : kReadyMsg);
  // Game opens in the stopped state (g_running == false), so the Pause
  // menu item should reflect that on launch even if the .rc had IDM_PAUSE
  // unchecked. SyncPauseMenuCheck folds both g_running and g_paused into
  // the single Pause indicator.
  SyncPauseMenuCheck(hWnd);
  // ~60 fps tick rate.
  if (SetTimer(hWnd, TIMER_GAME, kGameTickDelay, nullptr) == 0) {
    return false;
  }
  if (!InitSegmentDisplays(hWnd)) {
    return false;
  }
  InitRackets(hWnd);
  InitBall(hWnd);
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
