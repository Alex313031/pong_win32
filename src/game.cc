// Main game logic, handles CPU opponent and user input

#include "game.h"

#include "globals.h"
#include "resource.h"
#include "utils.h"

namespace {

// Classic LED look: bright red lit segment over a dim red unlit ghost so the
// digit silhouette is always visible (this is what Win 3.1/95 Minesweeper and
// most pocket calculators do). Painting unlit segments instead of leaving them
// as bare background also means we don't need a separate "8 8 8" backdrop pass.
constexpr COLORREF kSegOn  = RGB(255, 0, 0);
constexpr COLORREF kSegOff = RGB(50, 0, 0);

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

// kPlayfieldDividerY is the y of the 1-px horizontal divider that visually
// separates the score / state-message strip from the playfield below it.
// kPlayfieldTopY sits one row beneath it and is what the center line,
// rackets, and ball all clamp / bounce against, so the divider always
// stays untouched on top - no center-line dash or paddle ever overdraws it.
constexpr int kPlayfieldDividerY          = kEdgeMarginY + kDigitH + kEdgeMarginY;
constexpr COLORREF kPlayfieldDividerColor = RGB_LTGREY;
constexpr int kPlayfieldTopY              = kPlayfieldDividerY + 1;

// Message area. A 1-px frame between the two score displays, same height
// as the digits, used later to render state-message text (READY, GAME OVER,
// etc.). Inset from each display by kEdgeMarginX so it sits visually
// balanced between them at the same x-padding the displays use on the
// outside.
constexpr COLORREF kMessageAreaColor = RGB_LTGREY;

// Court center line. White dashes on g_bkg_color, drawn vertically through
// the midpoint of the client area. The top is clamped to kPlayfieldTopY so
// it can't intrude on the score / state-message strip; kCenterLineMarginY
// is the gap left at the bottom of the client so the line doesn't run all
// the way to the chrome / status bar edge. kCenterLineThickness is the
// dash width. kCenterLineDashCount is the *fixed* number of dashes - dash
// and gap heights are derived from the available vertical space at draw
// time so a resize keeps the count constant and just stretches the spacing.
constexpr COLORREF kCenterLineColor = RGB_LTGREY;
constexpr int kCenterLineMarginY    = 0;
constexpr int kCenterLineThickness  = 3;
constexpr int kCenterLineDashCount  = 22;

// Rackets. Two white rectangles, one anchored at each side. kRacketEdgeMarginX
// is the gap from the window edge to the racket's outer side. kRacketStepPx
// is how far the player's racket moves per WM_TIMER tick while an arrow key
// is held; kMachineRacketStepPx is the same for the CPU's tracking AI. Tick
// rate is ~60fps (see kGameTickDelay / SetTimer in InitApp), so step * 60 is
// roughly the racket's vertical px/sec. Splitting the two lets us nerf or
// buff the AI without touching the player's responsiveness.
// Player rackets are green, machine rackets are blue - colour follows the
// role, not the physical side, so toggling g_player_on_left swaps which
// side is green and which is blue.
constexpr COLORREF kPlayerRacketColor  = RGB_GREEN;
constexpr COLORREF kMachineRacketColor = RGB_BLUE;
constexpr int kRacketW                 = 14;
constexpr int kRacketH                 = 80;
constexpr int kRacketEdgeMarginX       = 18;
constexpr int kRacketStepPx            = 6;
constexpr int kMachineRacketStepPx     = kRacketStepPx / 2;

// Ball. Square, white, kBallSize on a side. kBallSpeed is the magnitude of
// the per-tick velocity vector; (dx, dy) decomposes it via cos/sin so the
// ball travels at the same speed regardless of launch angle. Position is
// float so accumulated drift from int rounding doesn't bend the trajectory
// over many frames. At ~60fps, kBallSpeed * 60 is roughly px/sec.
constexpr COLORREF kBallColor = RGB(255, 255, 255);
constexpr int kBallSize       = 14;
constexpr float kBallSpeed    = 6.0f;

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
bool g_player_on_left = false;
int g_left_racket_y   = -1;
int g_right_racket_y  = -1;

// Ball state. g_ball_spawned gates Draw/Tick - we can't reuse a negative
// coordinate as a sentinel here (unlike the rackets) because the ball is
// allowed to travel off the left edge, where g_ball_x legitimately goes
// negative. Without the explicit flag, a left-bound ball would be mistaken
// for "needs spawning" the moment it crossed x=0 and respawn forever, while
// a right-bound ball would never satisfy the sentinel and stay gone.
// Coordinates and velocity are float so sub-pixel motion accumulates
// correctly across frames (an int dx/dy at non-zero angles would round each
// frame and slowly bend the trajectory toward the nearest axis).
bool g_ball_spawned = false;
float g_ball_x      = 0.0f;
float g_ball_y      = 0.0f;
float g_ball_dx     = 0.0f;
float g_ball_dy     = 0.0f;

// PRNG for picking the ball's initial direction. random_device-seeded so
// successive runs don't keep launching the ball the same way.
std::mt19937 g_rng{std::random_device{}()};

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

// Bounding rect of a display in client coords. left_side picks the top-left
// vs top-right slot. Centralized so Update's invalidation and Draw's
// positioning can't drift apart.
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

// Display rect for the player's score (for_player == true) or the machine's
// (false). The player's score lives over their own racket, so it swaps sides
// with g_player_on_left. Both DrawSegmentDisplays and UpdateSegmentDisplay's
// invalidation route through here.
RECT DisplayRectFor(int client_width, bool for_player) {
  const bool left_side = (g_player_on_left == for_player);
  return DisplayRect(client_width, left_side);
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

// Applies a target y to one racket: clamps to the playfield, updates the
// state, and invalidates just the union of the old and new positions in
// that racket's column (so neither score displays, the centre line, nor
// the other racket end up in the dirty region).
void MoveRacket(HWND hWnd, int* racket_y, bool is_left, int target_y) {
  const int old_y = *racket_y;
  const int new_y = ClampRacketY(target_y);
  if (new_y == old_y) {
    return;
  }
  *racket_y = new_y;
  RECT r;
  r.left   = RacketX(cxClient, is_left);
  r.right  = r.left + kRacketW;
  r.top    = (old_y < new_y) ? old_y : new_y;
  r.bottom = ((old_y > new_y) ? old_y : new_y) + kRacketH;
  InvalidateRect(hWnd, &r, FALSE);
}

// Reads arrow-key state and moves the player's racket. Gated on our window
// being foreground so a key held while another app is active doesn't drive
// our paddle.
void TickPlayerRacket(HWND hWnd) {
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
  int* y     = g_player_on_left ? &g_left_racket_y : &g_right_racket_y;
  int target = *y;
  if (up) {
    target -= kRacketStepPx;
  }
  if (down) {
    target += kRacketStepPx;
  }
  MoveRacket(hWnd, y, g_player_on_left, target);
}

// CPU racket AI: track the ball's centre y, capped at kMachineRacketStepPx
// per tick. A small dead-zone of half a step prevents 1-pixel back-and-forth
// jitter when the racket is already on top of the ball. Tracks regardless
// of which way the ball is moving - same as the original Pong CPU; if we
// ever want to make it more humanly imperfect, this is where to do it.
void TickMachineRacket(HWND hWnd) {
  if (!g_ball_spawned) {
    return;
  }
  const bool machine_on_left = !g_player_on_left;
  int* y                     = machine_on_left ? &g_left_racket_y
                                               : &g_right_racket_y;
  const float ball_cy        = g_ball_y + 0.5f * kBallSize;
  const float racket_cy      = *y + 0.5f * kRacketH;
  const float diff           = ball_cy - racket_cy;
  const float dead_zone      = 0.5f * kMachineRacketStepPx;
  if (diff > dead_zone) {
    MoveRacket(hWnd, y, machine_on_left, *y + kMachineRacketStepPx);
  } else if (diff < -dead_zone) {
    MoveRacket(hWnd, y, machine_on_left, *y - kMachineRacketStepPx);
  }
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
  g_ball_x = 0.5f * cxClient - 0.5f * kBallSize;
  g_ball_y = kPlayfieldTopY + 0.5f * (cyClient - kPlayfieldTopY) - 0.5f * kBallSize;
  std::uniform_real_distribution<float> angle_dist(0.0f, static_cast<float>(kMaxLaunchAngle));
  const float angle = angle_dist(g_rng);
  // Low bit of mt19937 output is a fine 50/50 coin flip; one each for the
  // horizontal and vertical sign so all four quadrants are reachable.
  const float sx = (g_rng() & 1u) ? 1.0f : -1.0f;
  const float sy = (g_rng() & 1u) ? 1.0f : -1.0f;
  g_ball_dx      = sx * kBallSpeed * std::cos(angle);
  g_ball_dy      = sy * kBallSpeed * std::sin(angle);
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
  RECT r = DisplayRectFor(cxClient, player_display);
  InvalidateRect(mainHwnd, &r, FALSE);
}

void DrawSegmentDisplays(HDC hdc, const RECT& client) {
  const int width      = client.right - client.left;
  const RECT player_r  = DisplayRectFor(width, /*for_player=*/true);
  const RECT machine_r = DisplayRectFor(width, /*for_player=*/false);
  DrawOneDisplay(hdc, player_r.left,  player_r.top,  g_player_score);
  DrawOneDisplay(hdc, machine_r.left, machine_r.top, g_machine_score);
}

void DrawCenterLine(HDC hdc, const RECT& client) {
  // Centered on the exact horizontal midpoint of the client area. The -/2
  // before adding the thickness back keeps the line symmetric around that
  // midpoint for any even kCenterLineThickness; for odd values the line is
  // off by half a pixel, which is unavoidable in integer coords.
  const int midX   = (client.left + client.right) / 2;
  const int x_left = midX - kCenterLineThickness / 2;
  const int top    = client.top + kPlayfieldTopY;
  const int bottom = client.bottom - kCenterLineMarginY;
  const int total  = bottom - top;
  if (total <= 0 || kCenterLineDashCount <= 0) {
    return;
  }
  // Layout: N dashes with N-1 gaps between them, dash-h == gap-h. That's
  // 2N - 1 equal slots packed into `total`. Computing each slot's bounds
  // as (i * total) / slots inside the loop lets the integer division
  // spread any leftover pixels evenly across the dashes instead of
  // dumping them at one end.
  const int slots = 2 * kCenterLineDashCount - 1;
  HBRUSH hbr      = CreateSolidBrush(kCenterLineColor);
  for (int i = 0; i < kCenterLineDashCount; ++i) {
    RECT r;
    r.left   = x_left;
    r.right  = x_left + kCenterLineThickness;
    r.top    = top + (2 * i) * total / slots;
    r.bottom = top + (2 * i + 1) * total / slots;
    FillRect(hdc, &r, hbr);
  }
  DeleteObject(hbr);
}

void DrawPlayfieldDivider(HDC hdc, const RECT& client) {
  // X range spans the outer edges of both rackets, so when a racket is at
  // its highest position (top edge flush against kPlayfieldTopY) the
  // divider sits directly above its full width - the racket has something
  // to "hit against". kRacketEdgeMarginX is the gap from the client's edge
  // to the racket's outer side, mirrored on the right.
  const int client_w = client.right - client.left;
  RECT r;
  r.left   = client.left + kRacketEdgeMarginX;
  r.right  = client.left + client_w - kRacketEdgeMarginX;
  r.top    = client.top + kPlayfieldDividerY;
  r.bottom = r.top + 1;
  if (r.right <= r.left) {
    return;
  }
  HBRUSH hbr = CreateSolidBrush(kPlayfieldDividerColor);
  FillRect(hdc, &r, hbr);
  DeleteObject(hbr);
}

void DrawMessageArea(HDC hdc, const RECT& client) {
  // Inner gap between each score display and the message area mirrors the
  // gap between each display and the window edge (kEdgeMarginX), so the
  // message area sits visually balanced. Height tracks kDigitH so the
  // baseline lines up with the score digits at y = kEdgeMarginY.
  const int client_w = client.right - client.left;
  RECT r;
  r.left   = client.left + 2 * kEdgeMarginX + kDisplayW;
  r.right  = client.left + client_w - 2 * kEdgeMarginX - kDisplayW;
  r.top    = client.top + kEdgeMarginY;
  r.bottom = r.top + kDigitH;
  if (r.right <= r.left) {
    return;
  }
  // FrameRect draws a 1-px outline using the brush's colour, leaving the
  // interior untouched - exactly what we want for an empty text box.
  HBRUSH hbr = CreateSolidBrush(kMessageAreaColor);
  FrameRect(hdc, &r, hbr);
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
  // and skip the input/AI step so the rackets appear immediately on this
  // frame and start tracking next frame - simpler than threading state for
  // "just centred".
  if (g_left_racket_y < 0 || g_right_racket_y < 0) {
    if (cyClient <= 0) {
      return;
    }
    CenterRackets();
    InvalidateRect(hWnd, nullptr, FALSE);
    return;
  }
  TickPlayerRacket(hWnd);
  TickMachineRacket(hWnd);
}

void DrawRackets(HDC hdc, const RECT& client) {
  if (g_left_racket_y < 0 || g_right_racket_y < 0) {
    return;
  }
  const int width = client.right - client.left;
  const RECT lr   = RacketRect(width, /*left_side=*/true,  g_left_racket_y);
  const RECT rr   = RacketRect(width, /*left_side=*/false, g_right_racket_y);
  // Colour by role rather than position: the player's racket is always
  // green and the machine's always blue, no matter which side they're on.
  const COLORREF left_color  = g_player_on_left ? kPlayerRacketColor
                                                : kMachineRacketColor;
  const COLORREF right_color = g_player_on_left ? kMachineRacketColor
                                                : kPlayerRacketColor;
  HBRUSH lbr = CreateSolidBrush(left_color);
  HBRUSH rbr = CreateSolidBrush(right_color);
  FillRect(hdc, &lr, lbr);
  FillRect(hdc, &rr, rbr);
  DeleteObject(lbr);
  DeleteObject(rbr);
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
  const float old_x = g_ball_x;
  const float old_y = g_ball_y;
  float nx          = old_x + g_ball_dx;
  float ny          = old_y + g_ball_dy;

  // Top / bottom bounce. Reflect the overshoot back into the playfield
  // (nx' = 2*wall - nx) instead of clamping, so the ball preserves its
  // sub-frame momentum and bouncing speed stays consistent.
  if (ny < kPlayfieldTopY) {
    ny        = 2.0f * kPlayfieldTopY - ny;
    g_ball_dy = -g_ball_dy;
  } else if (ny + kBallSize > cyClient) {
    ny        = 2.0f * (cyClient - kBallSize) - ny;
    g_ball_dy = -g_ball_dy;
  }

  // Racket bounce. AABB overlap test gated on the ball moving INTO the
  // racket's playfield-facing edge (dx sign) - if the racket happens to
  // overlap the ball while the ball is already moving away (after a
  // previous bounce), we don't flip the direction back.
  if (g_left_racket_y >= 0) {
    const float lr_left   = kRacketEdgeMarginX;
    const float lr_right  = lr_left + kRacketW;
    const float lr_top    = g_left_racket_y;
    const float lr_bottom = lr_top + kRacketH;
    if (g_ball_dx < 0.0f &&
        nx < lr_right && nx + kBallSize > lr_left &&
        ny + kBallSize > lr_top && ny < lr_bottom) {
      nx        = 2.0f * lr_right - nx;
      g_ball_dx = -g_ball_dx;
    }
  }
  if (g_right_racket_y >= 0) {
    const float rr_right  = cxClient - kRacketEdgeMarginX;
    const float rr_left   = rr_right - kRacketW;
    const float rr_top    = g_right_racket_y;
    const float rr_bottom = rr_top + kRacketH;
    if (g_ball_dx > 0.0f &&
        nx + kBallSize > rr_left && nx < rr_right &&
        ny + kBallSize > rr_top && ny < rr_bottom) {
      nx        = 2.0f * (rr_left - kBallSize) - nx;
      g_ball_dx = -g_ball_dx;
    }
  }

  if (nx == old_x && ny == old_y) {
    return;
  }
  g_ball_x = nx;
  g_ball_y = ny;

  // Scoring: a ball that has fully exited the client area on one side hands
  // a point to the *other* side, then respawns at centre with a fresh random
  // angle. We treat the screen edge as the scoring line rather than the
  // racket's x, since once the ball is past the racket the racket has
  // already missed it - the rest of the travel is just animation.
  const bool off_left  = (g_ball_x + kBallSize < 0.0f);
  const bool off_right = (g_ball_x > static_cast<float>(cxClient));
  if (off_left || off_right) {
    // Off-left means the right side scored, and vice versa.
    const bool left_side_scored   = off_right;
    const bool player_scored      = (g_player_on_left == left_side_scored);
    const unsigned int next_score = player_scored ? g_player_score + 1
                                                  : g_machine_score + 1;
    UpdateSegmentDisplay(player_scored, next_score);
    SpawnBall();
    // Full-client invalidate: the ball jumped from off-screen to the centre,
    // so a tight dirty rect would have to span half the playfield - cheaper
    // (and simpler) to just repaint everything for the one frame.
    InvalidateRect(hWnd, nullptr, FALSE);
    return;
  }

  // Invalidate the bounding rect of the ball's old + new positions. floor
  // on min, ceil on max so the dirty region covers every pixel either rect
  // could touch even with fractional positions.
  const float min_x = (old_x < nx) ? old_x : nx;
  const float max_x = (old_x > nx) ? old_x : nx;
  const float min_y = (old_y < ny) ? old_y : ny;
  const float max_y = (old_y > ny) ? old_y : ny;
  RECT r;
  r.left   = static_cast<int>(std::floor(min_x));
  r.top    = static_cast<int>(std::floor(min_y));
  r.right  = static_cast<int>(std::ceil(max_x + kBallSize));
  r.bottom = static_cast<int>(std::ceil(max_y + kBallSize));
  InvalidateRect(hWnd, &r, FALSE);
}

void DrawBall(HDC hdc, const RECT& client) {
  (void)client;
  if (!g_ball_spawned) {
    return;
  }
  const int x = static_cast<int>(std::floor(g_ball_x));
  const int y = static_cast<int>(std::floor(g_ball_y));
  RECT r;
  r.left   = x;
  r.top    = y;
  r.right  = x + kBallSize;
  r.bottom = y + kBallSize;
  HBRUSH hbr = CreateSolidBrush(kBallColor);
  FillRect(hdc, &r, hbr);
  DeleteObject(hbr);
  // Knock out the four corner pixels so the silhouette reads as a slightly
  // rounded square rather than a hard rectangle. Painting them back to
  // g_bkg_color works because the dirty rect was just filled with that
  // colour in WM_PAINT before us - the corner pixels were re-covered by
  // FillRect above and we're now undoing that for those four pixels.
  SetPixel(hdc, x,                 y,                 g_bkg_color);
  SetPixel(hdc, x + kBallSize - 1, y,                 g_bkg_color);
  SetPixel(hdc, x,                 y + kBallSize - 1, g_bkg_color);
  SetPixel(hdc, x + kBallSize - 1, y + kBallSize - 1, g_bkg_color);
}
