// Main game logic, handles CPU opponent and user input

#include "game.h"

#include "globals.h"
#include "resource.h"

namespace {

// Classic LED look: bright red lit segment over a dim red unlit ghost so the
// digit silhouette is always visible (this is what Win 3.1/95 Minesweeper and
// most pocket calculators do). Painting unlit segments instead of leaving them
// as bare background also means we don't need a separate "8 8 8" backdrop pass.
constexpr COLORREF kSegOn  = RGB(255, 40, 40);
constexpr COLORREF kSegOff = RGB(48, 0, 0);

// Per-digit segment bitmasks. Bit layout: 0=a 1=b 2=c 3=d 4=e 5=f 6=g, matching
// the conventional 7-seg labeling:
//      aaa
//     f   b
//      ggg
//     e   c
//      ddd
constexpr unsigned char kDigitSegs[10] = {
    0b0111111, // 0: a b c d e f
    0b0000110, // 1: b c
    0b1011011, // 2: a b d e g
    0b1001111, // 3: a b c d g
    0b1100110, // 4: b c f g
    0b1101101, // 5: a c d f g
    0b1111101, // 6: a c d e f g
    0b0000111, // 7: a b c
    0b1111111, // 8: a b c d e f g
    0b1101111, // 9: a b c d f g
};

// Geometry for one 3-digit display. These are tuned to look right at the
// default 1024x800 canvas; if we later want them to scale with the window,
// this is the single place to thread cxClient through. kSegT is even so the
// middle horizontal's half-thickness (t/2) is a whole pixel - odd thickness
// rounds asymmetrically and the middle ends up a row thinner than top/bottom.
constexpr int kDigitW     = 24;
constexpr int kDigitH     = 48;
constexpr int kSegT       = 6;
// Perpendicular inset at each segment-to-segment join. The classic LED look
// has a visible hairline between adjacent segments rather than them merging
// into a single blob; pulling each segment's slanted edges inward by this
// many pixels along their axis produces that hairline. Since every join is
// at 45 degrees, axial offset g yields g*sqrt(2) px perpendicular - so 2
// here is ~2.8 px visible gap.
constexpr int kSegGap     = 2;
constexpr int kDigitGap   = 5;
// X/Y margins from the window edges to the display. Split so the displays
// can be pushed further inward horizontally (clear of the paddle columns)
// without also pushing them down away from the top edge, and vice versa.
constexpr int kEdgeMarginX = 48;
constexpr int kEdgeMarginY = 14;
constexpr int kDisplayW    = kDigitW * 3 + kDigitGap * 2;

// Top edge of the playfield: kEdgeMarginY below the bottom of the score
// displays. Acts as an upper bound for the center line, racket travel, and
// (eventually) the ball, so the upper-center strip stays clear for the
// state-message text and the rackets can't drift up into the score area.
constexpr int kPlayfieldTopY = kEdgeMarginY + kDigitH + kEdgeMarginY;

// Court center line. White on g_bkg_color, drawn vertically through the
// midpoint of the client area. The line's top is clamped to kPlayfieldTopY
// so it can't intrude on the score / state-message strip; kCenterLineMarginY
// is the gap left at the bottom of the client so the line doesn't run all
// the way to the chrome / status bar edge. kCenterLineThickness is its width.
constexpr COLORREF kCenterLineColor = RGB(255, 255, 255);
constexpr int kCenterLineMarginY    = 14;
constexpr int kCenterLineThickness  = 6;

// Rackets. Two white rectangles, one anchored at each side. kRacketEdgeMarginX
// is the gap from the window edge to the racket's outer side; kRacketStepPx
// is how far the player's racket moves per WM_TIMER tick while an arrow key
// is held. Tick rate is ~60fps (see kGameTickDelay / SetTimer in InitApp), so
// step * 60 is roughly the racket's vertical px/sec.
constexpr COLORREF kRacketColor   = RGB(255, 255, 255);
constexpr int kRacketW            = 14;
constexpr int kRacketH            = 90;
constexpr int kRacketEdgeMarginX  = 20;
constexpr int kRacketStepPx       = 6;

// Ball. Square, white, kBallSize on a side. kBallStepPx is the per-axis
// step *along the launch vector* - at angle 0 the ball moves kBallStepPx px
// per tick horizontally; at non-zero angles dx/dy round to ints so total
// speed dips slightly off the nominal (sqrt(2)/2 * step on each axis at 45),
// which is fine until we move to float ball coords.
constexpr COLORREF kBallColor = RGB(255, 255, 255);
constexpr int kBallSize       = 14;
constexpr int kBallStepPx     = 6;

// Launch angle bound. The ball comes off centre at a uniformly random angle
// between 0 and this value off the horizontal axis, with independent coin
// flips picking left/right and up/down. 45 keeps the motion noticeably
// horizontal-dominant - the original Pong feel - while still giving the
// player enough vertical surprise to make tracking it interesting.
constexpr double kPi             = 3.14159265358979323846;
constexpr double kMaxLaunchAngle = kPi / 4.0;

// Live score state. File-scope so InitSegmentDisplays / UpdateSegmentDisplay
// can mutate without exposing the variables to the rest of the program -
// callers go through the API, the renderer goes through DrawSegmentDisplays.
unsigned int g_player_score  = 0;
unsigned int g_machine_score = 0;

// Racket state. -1 means "not yet centered" - WM_CREATE / InitRackets fires
// before WM_SIZE so we can't pick a y until cyClient is known. The first
// WM_TIMER tick where it is non-zero does the centering.
bool g_player_on_left = true;
int g_left_racket_y   = -1;
int g_right_racket_y  = -1;

// Ball state. g_ball_spawned gates Draw/Tick - we can't reuse a negative
// coordinate as a sentinel here (unlike the rackets) because the ball is
// allowed to travel off the left edge, where g_ball_x legitimately goes
// negative. Without the explicit flag, a left-bound ball would be mistaken
// for "needs spawning" the moment it crossed x=0 and respawn forever, while
// a right-bound ball would never satisfy the sentinel and stay gone.
// dx/dy are signed per-tick step deltas; dy stays 0 until bounce / paddle-
// hit logic gives the ball a vertical component.
bool g_ball_spawned = false;
int g_ball_x        = 0;
int g_ball_y        = 0;
int g_ball_dx       = 0;
int g_ball_dy       = 0;

// PRNG for picking the ball's initial direction. random_device-seeded so
// successive runs don't keep launching the ball the same way.
std::mt19937 g_rng{std::random_device{}()};

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

// Draws a single digit in the cell at (x, y). `digit` outside 0-9 paints all
// segments off (useful if we ever want to show blanks).
//
// Geometry: every lip in the digit is a 45-degree chamfer. Top/bottom and the
// four verticals are trapezoids with the flat side along the digit's outer
// edge; the middle horizontal is a hexagon with 45 tips at each end. Where
// a vertical meets the middle, the vertical's bottom (or top) is a 45 lip
// that extends past the middle's 45 shoulder along the same line - so they
// share part of one continuous diagonal edge, not two slants at different
// angles.
//
// kSegGap pulls every slanted edge inward along the segment's main axis so a
// thin background-coloured hairline remains where adjacent segments would
// otherwise share an edge - that's the "off LEDs are separate elements" look
// of a real display.
void DrawDigit(HDC hdc, int x, int y, int w, int h, int t, int digit) {
  const unsigned char mask = (digit >= 0 && digit <= 9) ? kDigitSegs[digit] : 0;
  const int midY           = y + h / 2;
  const int halft          = t / 2;
  const int g              = kSegGap;
  auto col                 = [&](int bit) { return (mask & (1u << bit)) ? kSegOn : kSegOff; };

  // 'a' - top horizontal trapezoid. Flat top sits on the digit's top edge;
  // the bottom edge is shorter by t on each side, with 45 lips angling
  // down-into-the-digit so 'f' and 'b' can fit alongside.
  {
    const POINT pts[4] = {
        {x + g,         y    },
        {x + w - g,     y    },
        {x + w - t - g, y + t},
        {x + t + g,     y + t},
    };
    FillPolygon(hdc, pts, 4, col(0));
  }
  // 'b' - upper-right vertical trapezoid. Flat side on the digit's right
  // edge; lips angle into the digit at top (meeting 'a') and bottom
  // (meeting 'g'). Bottom lip ends at midY - t so the 45 slant lines up
  // with 'g's top-right tip.
  {
    const POINT pts[4] = {
        {x + w,     y + g       },
        {x + w - t, y + t + g   },
        {x + w - t, midY - t - g},
        {x + w,     midY - g    },
    };
    FillPolygon(hdc, pts, 4, col(1));
  }
  // 'c' - lower-right vertical trapezoid. Mirror of 'b' below the middle.
  {
    const POINT pts[4] = {
        {x + w,     midY + g    },
        {x + w - t, midY + t + g},
        {x + w - t, y + h - t - g},
        {x + w,     y + h - g   },
    };
    FillPolygon(hdc, pts, 4, col(2));
  }
  // 'd' - bottom horizontal trapezoid. Mirror of 'a' along the digit's
  // bottom edge.
  {
    const POINT pts[4] = {
        {x + t + g,     y + h - t},
        {x + w - t - g, y + h - t},
        {x + w - g,     y + h    },
        {x + g,         y + h    },
    };
    FillPolygon(hdc, pts, 4, col(3));
  }
  // 'e' - lower-left vertical trapezoid. Mirror of 'c'.
  {
    const POINT pts[4] = {
        {x,     midY + g    },
        {x + t, midY + t + g},
        {x + t, y + h - t - g},
        {x,     y + h - g   },
    };
    FillPolygon(hdc, pts, 4, col(4));
  }
  // 'f' - upper-left vertical trapezoid. Mirror of 'b'.
  {
    const POINT pts[4] = {
        {x,     y + g       },
        {x + t, y + t + g   },
        {x + t, midY - t - g},
        {x,     midY - g    },
    };
    FillPolygon(hdc, pts, 4, col(5));
  }
  // 'g' - middle horizontal hexagon. 45 tips: shoulders are halft from each
  // tip so the slant has slope 1. Body (between shoulders) is shorter than
  // 'a'/'d' by t, which gives the middle the visibly narrower silhouette
  // of a classic 7-seg.
  {
    const POINT pts[6] = {
        {x + g,             midY        },
        {x + halft + g,     midY - halft},
        {x + w - halft - g, midY - halft},
        {x + w - g,         midY        },
        {x + w - halft - g, midY + halft},
        {x + halft + g,     midY + halft},
    };
    FillPolygon(hdc, pts, 6, col(6));
  }
}

// Bounding rect of a display in client coords. left_side picks the player
// (top-left) vs the machine (top-right). Centralized so Update's invalidation
// and Draw's positioning can't drift apart.
RECT DisplayRect(int client_width, bool left_side) {
  RECT r;
  r.top    = kEdgeMarginY;
  r.bottom = kEdgeMarginY + kDigitH;
  if (left_side) {
    r.left  = kEdgeMarginX;
    r.right = kEdgeMarginX + kDisplayW;
  } else {
    r.right = client_width - kEdgeMarginX;
    r.left  = r.right - kDisplayW;
  }
  return r;
}

void DrawOneDisplay(HDC hdc, int origin_x, int origin_y, unsigned int score) {
  if (score > kMaxNumPoints) {
    score = kMaxNumPoints;
  }
  // Always show three digits with leading zeros - matches Minesweeper's
  // LED counters and avoids the score box visually shifting as it grows.
  const int d_hundreds = static_cast<int>(score / 100);
  const int d_tens     = static_cast<int>((score / 10) % 10);
  const int d_ones     = static_cast<int>(score % 10);
  const int stride     = kDigitW + kDigitGap;
  DrawDigit(hdc, origin_x,              origin_y, kDigitW, kDigitH, kSegT, d_hundreds);
  DrawDigit(hdc, origin_x + stride,     origin_y, kDigitW, kDigitH, kSegT, d_tens);
  DrawDigit(hdc, origin_x + stride * 2, origin_y, kDigitW, kDigitH, kSegT, d_ones);
}

// Left-edge x of the racket on the given side.
int RacketX(int client_width, bool left_side) {
  return left_side ? kRacketEdgeMarginX
                   : client_width - kRacketEdgeMarginX - kRacketW;
}

// Builds the rect occupied by a racket given its top-edge y.
RECT RacketRect(int client_width, bool left_side, int y) {
  RECT r;
  r.left   = RacketX(client_width, left_side);
  r.right  = r.left + kRacketW;
  r.top    = y;
  r.bottom = y + kRacketH;
  return r;
}

int ClampRacketY(int y) {
  if (y < kPlayfieldTopY) {
    return kPlayfieldTopY;
  }
  if (cyClient > 0 && y > cyClient - kRacketH) {
    return cyClient - kRacketH;
  }
  return y;
}

// Sets both racket y's to the vertical centre of the *playfield* (the slice
// of the client area below kPlayfieldTopY), not the whole client - otherwise
// the racket would sit visually high because the score strip eats the top of
// the window. No-op when cyClient isn't known yet (the lazy-init path in
// TickRackets retries every frame until WM_SIZE has fired).
void CenterRackets() {
  if (cyClient <= 0) {
    return;
  }
  const int playfield_h = cyClient - kPlayfieldTopY;
  const int centered    = kPlayfieldTopY + (playfield_h - kRacketH) / 2;
  g_left_racket_y       = centered;
  g_right_racket_y      = centered;
}

// Places the ball at the centre of the playfield and gives it a velocity at
// a random angle within [0, kMaxLaunchAngle] off horizontal, with
// independent coin flips for x and y sign. No-op until cxClient/cyClient
// are valid; sets g_ball_spawned only on success so callers can use that as
// the spawned/unspawned flag without inspecting coordinates.
void SpawnBall() {
  if (cxClient <= 0 || cyClient <= 0) {
    return;
  }
  g_ball_x = cxClient / 2 - kBallSize / 2;
  g_ball_y = kPlayfieldTopY + (cyClient - kPlayfieldTopY) / 2 - kBallSize / 2;
  std::uniform_real_distribution<double> angle_dist(0.0, kMaxLaunchAngle);
  const double angle = angle_dist(g_rng);
  // Low bit of mt19937 output is a fine 50/50 coin flip; one each for the
  // horizontal and vertical sign so all four quadrants are reachable.
  const int sx   = (g_rng() & 1u) ? 1 : -1;
  const int sy   = (g_rng() & 1u) ? 1 : -1;
  g_ball_dx      = sx * static_cast<int>(std::lround(kBallStepPx * std::cos(angle)));
  g_ball_dy      = sy * static_cast<int>(std::lround(kBallStepPx * std::sin(angle)));
  g_ball_spawned = true;
}

} // namespace

bool InitSegmentDisplays(HWND hWnd) {
  if (hWnd == nullptr) {
    return false;
  }
  g_player_score  = 0;
  g_machine_score = 0;
  // Force the displays to appear on the next paint cycle. FALSE = don't
  // erase the background since we own WM_ERASEBKGND and WM_PAINT already
  // refills with g_bkg_color before drawing the digits on top.
  InvalidateRect(hWnd, nullptr, FALSE);
  return true;
}

void UpdateSegmentDisplay(bool player_display, unsigned int score) {
  if (score > kMaxNumPoints) {
    score = kMaxNumPoints;
  }
  if (player_display) {
    g_player_score = score;
  } else {
    g_machine_score = score;
  }
  if (mainHwnd == nullptr) {
    return;
  }
  // Invalidate just the affected display's rect rather than the whole
  // client - keeps a 60 fps tick of score updates from forcing a full
  // window repaint when only six digits' worth of pixels could change.
  RECT r = DisplayRect(cxClient, player_display);
  InvalidateRect(mainHwnd, &r, FALSE);
}

void DrawSegmentDisplays(HDC hdc, const RECT& client) {
  const int width = client.right - client.left;
  const RECT lr   = DisplayRect(width, /*left_side=*/true);
  const RECT rr   = DisplayRect(width, /*left_side=*/false);
  DrawOneDisplay(hdc, lr.left, lr.top, g_player_score);
  DrawOneDisplay(hdc, rr.left, rr.top, g_machine_score);
}

void DrawCenterLine(HDC hdc, const RECT& client) {
  // Centered on the exact horizontal midpoint of the client area. The -/2
  // before adding the thickness back keeps the line symmetric around that
  // midpoint for any even kCenterLineThickness; for odd values the line is
  // off by half a pixel, which is unavoidable in integer coords.
  const int midX = (client.left + client.right) / 2;
  RECT r;
  r.left   = midX - kCenterLineThickness / 2;
  r.right  = r.left + kCenterLineThickness;
  r.top    = client.top + kPlayfieldTopY;
  r.bottom = client.bottom - kCenterLineMarginY;
  HBRUSH hbr = CreateSolidBrush(kCenterLineColor);
  FillRect(hdc, &r, hbr);
  DeleteObject(hbr);
}

void SetPlayerOnLeft(bool on_left) {
  g_player_on_left = on_left;
}

void InitRackets(HWND hWnd) {
  if (hWnd == nullptr) {
    return;
  }
  g_left_racket_y  = -1;
  g_right_racket_y = -1;
  // Try to centre now in case WM_SIZE has already run; if not, TickRackets
  // will do it on the first tick that sees a non-zero cyClient.
  CenterRackets();
  InvalidateRect(hWnd, nullptr, FALSE);
}

void TickRackets(HWND hWnd) {
  if (hWnd == nullptr) {
    return;
  }
  // Lazy-centre on the first tick that has a real cyClient. We invalidate
  // and skip the input read so the rackets appear immediately on this frame
  // and key handling starts the frame after - simpler than threading state
  // for "just centred".
  if (g_left_racket_y < 0 || g_right_racket_y < 0) {
    if (cyClient <= 0) {
      return;
    }
    CenterRackets();
    InvalidateRect(hWnd, nullptr, FALSE);
    return;
  }
  // GetAsyncKeyState reads the global keyboard state, so a key held while
  // another app is foreground would still register here. Gate on
  // foreground-ness to keep arrow presses in (say) the user's editor from
  // sliding our paddle around in the background.
  if (GetForegroundWindow() != hWnd) {
    return;
  }
  const bool up   = (GetAsyncKeyState(VK_UP)   & 0x8000) ||
                    (GetAsyncKeyState(VK_LEFT) & 0x8000);
  const bool down = (GetAsyncKeyState(VK_DOWN)  & 0x8000) ||
                    (GetAsyncKeyState(VK_RIGHT) & 0x8000);
  if (!up && !down) {
    return;
  }
  int* y = g_player_on_left ? &g_left_racket_y : &g_right_racket_y;
  const int old_y = *y;
  int new_y       = old_y;
  if (up) {
    new_y -= kRacketStepPx;
  }
  if (down) {
    new_y += kRacketStepPx;
  }
  new_y = ClampRacketY(new_y);
  if (new_y == old_y) {
    return;
  }
  *y = new_y;
  // Invalidate just the union of the racket's old and new positions in its
  // own column. Keeps the score displays, center line and the opposite
  // racket out of the dirty region so we don't repaint the whole window
  // every frame the player is moving.
  RECT r;
  r.left   = RacketX(cxClient, g_player_on_left);
  r.right  = r.left + kRacketW;
  r.top    = (old_y < new_y) ? old_y : new_y;
  r.bottom = ((old_y > new_y) ? old_y : new_y) + kRacketH;
  InvalidateRect(hWnd, &r, FALSE);
}

void DrawRackets(HDC hdc, const RECT& client) {
  if (g_left_racket_y < 0 || g_right_racket_y < 0) {
    return;
  }
  const int width = client.right - client.left;
  const RECT lr   = RacketRect(width, /*left_side=*/true,  g_left_racket_y);
  const RECT rr   = RacketRect(width, /*left_side=*/false, g_right_racket_y);
  HBRUSH hbr      = CreateSolidBrush(kRacketColor);
  FillRect(hdc, &lr, hbr);
  FillRect(hdc, &rr, hbr);
  DeleteObject(hbr);
}

void InitBall(HWND hWnd) {
  if (hWnd == nullptr) {
    return;
  }
  g_ball_spawned = false;
  g_ball_x       = 0;
  g_ball_y       = 0;
  g_ball_dx      = 0;
  g_ball_dy      = 0;
  // Try to spawn now in case cxClient/cyClient are already known; otherwise
  // TickBall will retry every frame until WM_SIZE has fired.
  SpawnBall();
  InvalidateRect(hWnd, nullptr, FALSE);
}

void TickBall(HWND hWnd) {
  if (hWnd == nullptr) {
    return;
  }
  // Lazy spawn on the first tick that has a real client size. Same dance as
  // the rackets: invalidate and skip movement this frame so the ball appears
  // at the centre before it starts moving.
  if (!g_ball_spawned) {
    SpawnBall();
    if (!g_ball_spawned) {
      return;
    }
    InvalidateRect(hWnd, nullptr, FALSE);
    return;
  }
  const int old_x = g_ball_x;
  const int old_y = g_ball_y;
  const int new_x = old_x + g_ball_dx;
  const int new_y = old_y + g_ball_dy;
  if (new_x == old_x && new_y == old_y) {
    return;
  }
  g_ball_x = new_x;
  g_ball_y = new_y;
  // Invalidate the bounding rect of the ball's old + new positions so a
  // single repaint clears the trail and draws the new frame.
  RECT r;
  r.left   = (old_x < new_x ? old_x : new_x);
  r.right  = (old_x > new_x ? old_x : new_x) + kBallSize;
  r.top    = (old_y < new_y ? old_y : new_y);
  r.bottom = (old_y > new_y ? old_y : new_y) + kBallSize;
  InvalidateRect(hWnd, &r, FALSE);
}

void DrawBall(HDC hdc, const RECT& client) {
  (void)client;
  if (!g_ball_spawned) {
    return;
  }
  RECT r;
  r.left   = g_ball_x;
  r.top    = g_ball_y;
  r.right  = r.left + kBallSize;
  r.bottom = r.top + kBallSize;
  HBRUSH hbr = CreateSolidBrush(kBallColor);
  FillRect(hdc, &r, hbr);
  DeleteObject(hbr);
}
