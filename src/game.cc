// Main game logic, handles CPU opponent and user input

#include "game.h"

#include "constants.h"
#include "globals.h"
#include "resource.h"
#include "sound.h"
#include "utils.h"

// Pause toggle (declared in globals.h). Lives here because TickRackets /
// TickBall are its only readers. volatile in case future work shifts the
// tick or input loop onto another thread.
volatile bool g_paused = false;

namespace {

// All tuning constants (colours, sizes, speeds, audio frequencies, the
// per-digit segment masks) live in constants.h so they can be tweaked in
// one place. This file owns the state and logic.

// Live score state. File-scope so InitSegmentDisplays / UpdateSegmentDisplay
// can mutate without exposing the variables to the rest of the program -
// callers go through the API, the renderer goes through DrawSegmentDisplays.
unsigned int g_player_score  = 0;
unsigned int g_machine_score = 0;

// Current message-area text. Set via SetMessage; rendered by DrawMessageArea
// alongside the frame. Empty string => frame only, no text.
std::wstring g_message;

// Racket state. -1 means "not yet centered" - WM_CREATE / InitRackets fires
// before WM_SIZE so we can't pick a y until cyClient is known. The first
// WM_TIMER tick where it is non-zero does the centering.
bool g_player_on_left = false;

// Speed / difficulty settings. Defaults to Med so the unconfigured app
// matches the raw *PxPerSec constants. SpeedMult() turns the Speed enum
// into a velocity multiplier for ball and rackets; LagFrames() turns the
// Difficulty enum into a count of ball-y history entries the CPU racket
// reads from instead of the live g_ball_y (see g_ai_history below).
Speed g_speed           = Speed::Med;
Difficulty g_difficulty = Difficulty::Med;

float SpeedMult() {
  switch (g_speed) {
    case Speed::Low:  return kSpeedMultLow;
    case Speed::High: return kSpeedMultHigh;
    case Speed::Med:
    default:          return kSpeedMultMed;
  }
}

int LagFrames() {
  switch (g_difficulty) {
    case Difficulty::Easy: return kAiLagFramesEasy;
    case Difficulty::Hard: return kAiLagFramesHard;
    case Difficulty::Med:
    default:               return kAiLagFramesMed;
  }
}

// Ring buffer of recent ball y positions for the CPU's prediction lag.
// TickBall pushes the post-bounce g_ball_y here every frame; the index
// rolls modulo kAiHistorySize. TickMachineRacket reads from
// (g_ai_history_idx - 1 - LagFrames()) so the AI is always aiming at
// where the ball was N frames ago. SpawnBall fills the buffer with the
// spawn y so the AI has a sensible read before any movement has
// occurred yet.
float g_ai_history[kAiHistorySize] = {};
int g_ai_history_idx               = 0;

// Racket Y coords are float so per-frame "speed * dt" deltas accumulate
// sub-pixel motion correctly across frames; we floor() when building the
// integer draw rect. Negative still serves as the "not yet centred"
// sentinel since clamped values never go that low.
float g_left_racket_y  = -1.0f;
float g_right_racket_y = -1.0f;

// Wall-clock delta tracking. QueryPerformanceCounter gives sub-microsecond
// resolution and is monotonic, so dt comes from real elapsed time rather
// than the (jittery) WM_TIMER cadence. The per-tick dt is clamped to
// kMaxDeltaSeconds (in constants.h) to keep a stall from teleporting the
// ball.
LARGE_INTEGER g_qpc_freq = {};
LARGE_INTEGER g_qpc_last = {};
bool g_qpc_initialized   = false;


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

// Fire-and-forget hit sound. PlaySoundW with SND_ASYNC hands the wav off
// to MSACM (it gets decoded by msadp32.acm on Win2k, which is why the
// build script encodes the wavs as MS-ADPCM) and returns immediately, so
// the game loop never blocks on the playback. SND_NODEFAULT keeps Windows
// from substituting its system "ding" if the resource can't be found.
// No-op when sound is muted.
//
// Caveat: PlaySoundW only sustains one async stream per process, so a
// second hit landing while another is still playing will cut the first.
// At 25 ms wavs and normal bounce spacing this is rarely audible. Music
// is on MCI (see sound.cc) so its loop is *not* contested by hits - the
// two audio paths use separate devices.
void PlayHit(UINT resource_id) {
  if (!g_sound_on) {
    return;
  }
  PlaySoundW(MAKEINTRESOURCEW(resource_id), g_hInstance,
             SND_RESOURCE | SND_ASYNC | SND_NODEFAULT);
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
void DrawDigit(HDC hdc, int cell_x, int cell_y, int cell_w, int cell_h,
               int thickness, int digit) {
  const unsigned char mask = (digit >= 0 && digit <= 9) ? kDigitSegs[digit] : 0;
  const int mid_y          = cell_y + cell_h / 2;
  const int half_thick     = thickness / 2;
  const int gap            = kSegGap;
  auto seg_color           = [&](int bit) {
    return (mask & (1u << bit)) ? kSegOn : kSegOff;
  };

  // 'a' - top horizontal trapezoid. Flat top sits on the digit's top edge;
  // the bottom edge is shorter by `thickness` on each side, with 45 lips
  // angling down-into-the-digit so 'f' and 'b' can fit alongside.
  {
    const POINT pts[4] = {
        {cell_x + gap,                     cell_y            },
        {cell_x + cell_w - gap,            cell_y            },
        {cell_x + cell_w - thickness - gap, cell_y + thickness},
        {cell_x + thickness + gap,         cell_y + thickness},
    };
    FillPolygon(hdc, pts, 4, seg_color(0));
  }
  // 'b' - upper-right vertical trapezoid. Flat side on the digit's right
  // edge; lips angle into the digit at top (meeting 'a') and bottom
  // (meeting 'g'). Bottom lip ends at mid_y - thickness so the 45 slant
  // lines up with 'g's top-right tip.
  {
    const POINT pts[4] = {
        {cell_x + cell_w,             cell_y + gap               },
        {cell_x + cell_w - thickness, cell_y + thickness + gap   },
        {cell_x + cell_w - thickness, mid_y - thickness - gap    },
        {cell_x + cell_w,             mid_y - gap                },
    };
    FillPolygon(hdc, pts, 4, seg_color(1));
  }
  // 'c' - lower-right vertical trapezoid. Mirror of 'b' below the middle.
  {
    const POINT pts[4] = {
        {cell_x + cell_w,             mid_y + gap                       },
        {cell_x + cell_w - thickness, mid_y + thickness + gap           },
        {cell_x + cell_w - thickness, cell_y + cell_h - thickness - gap},
        {cell_x + cell_w,             cell_y + cell_h - gap            },
    };
    FillPolygon(hdc, pts, 4, seg_color(2));
  }
  // 'd' - bottom horizontal trapezoid. Mirror of 'a' along the digit's
  // bottom edge.
  {
    const POINT pts[4] = {
        {cell_x + thickness + gap,         cell_y + cell_h - thickness},
        {cell_x + cell_w - thickness - gap, cell_y + cell_h - thickness},
        {cell_x + cell_w - gap,            cell_y + cell_h            },
        {cell_x + gap,                     cell_y + cell_h            },
    };
    FillPolygon(hdc, pts, 4, seg_color(3));
  }
  // 'e' - lower-left vertical trapezoid. Mirror of 'c'.
  {
    const POINT pts[4] = {
        {cell_x,             mid_y + gap                       },
        {cell_x + thickness, mid_y + thickness + gap           },
        {cell_x + thickness, cell_y + cell_h - thickness - gap},
        {cell_x,             cell_y + cell_h - gap            },
    };
    FillPolygon(hdc, pts, 4, seg_color(4));
  }
  // 'f' - upper-left vertical trapezoid. Mirror of 'b'.
  {
    const POINT pts[4] = {
        {cell_x,             cell_y + gap            },
        {cell_x + thickness, cell_y + thickness + gap},
        {cell_x + thickness, mid_y - thickness - gap },
        {cell_x,             mid_y - gap             },
    };
    FillPolygon(hdc, pts, 4, seg_color(5));
  }
  // 'g' - middle horizontal hexagon. 45 tips: shoulders are half_thick from
  // each tip so the slant has slope 1. Body (between shoulders) is shorter
  // than 'a'/'d' by `thickness`, which gives the middle the visibly
  // narrower silhouette of a classic 7-seg.
  {
    const POINT pts[6] = {
        {cell_x + gap,                      mid_y             },
        {cell_x + half_thick + gap,         mid_y - half_thick},
        {cell_x + cell_w - half_thick - gap, mid_y - half_thick},
        {cell_x + cell_w - gap,             mid_y             },
        {cell_x + cell_w - half_thick - gap, mid_y + half_thick},
        {cell_x + half_thick + gap,         mid_y + half_thick},
    };
    FillPolygon(hdc, pts, 6, seg_color(6));
  }
}

// Bounding rect of a display in client coords. left_side picks the top-left
// vs top-right slot. Centralized so Update's invalidation and Draw's
// positioning can't drift apart.
RECT DisplayRect(int client_width, bool left_side) {
  RECT rect;
  rect.top    = kEdgeMarginY;
  rect.bottom = kEdgeMarginY + kDigitH;
  if (left_side) {
    rect.left  = kEdgeMarginX;
    rect.right = kEdgeMarginX + kDisplayW;
  } else {
    rect.right = client_width - kEdgeMarginX;
    rect.left  = rect.right - kDisplayW;
  }
  return rect;
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

// Bounding rect of the state-message area between the two score displays.
// Shared by DrawMessageArea (frame + text) and SetMessage (invalidation).
RECT MessageAreaRect(int client_width) {
  RECT rect;
  rect.left   = 2 * kEdgeMarginX + kDisplayW;
  rect.right  = client_width - 2 * kEdgeMarginX - kDisplayW;
  rect.top    = kEdgeMarginY;
  rect.bottom = rect.top + kDigitH;
  return rect;
}

// Left-edge x of the racket on the given side.
int RacketX(int client_width, bool left_side) {
  return left_side ? kRacketEdgeMarginX
                   : client_width - kRacketEdgeMarginX - kRacketW;
}

// Builds the rect occupied by a racket given its top-edge y. top_y is
// float (the live racket state); we floor() to land on a pixel boundary.
RECT RacketRect(int client_width, bool left_side, float top_y) {
  const int top_pixel = static_cast<int>(std::floor(top_y));
  RECT rect;
  rect.left   = RacketX(client_width, left_side);
  rect.right  = rect.left + kRacketW;
  rect.top    = top_pixel;
  rect.bottom = top_pixel + kRacketH;
  return rect;
}

float ClampRacketY(float proposed_y) {
  if (proposed_y < static_cast<float>(kPlayfieldTopY)) {
    return static_cast<float>(kPlayfieldTopY);
  }
  if (cyClient > 0 && proposed_y > static_cast<float>(cyClient - kRacketH)) {
    return static_cast<float>(cyClient - kRacketH);
  }
  return proposed_y;
}

// Applies a target y to one racket: clamps to the playfield, updates the
// state, and invalidates just the union of the old and new positions in
// that racket's column (so neither score displays, the centre line, nor
// the other racket end up in the dirty region). Skips the invalidate if
// the integer pixel row didn't change - sub-pixel drift between frames
// is invisible and not worth a repaint.
void MoveRacket(HWND hWnd, float* racket_y, bool is_left, float target_y) {
  const float old_y = *racket_y;
  const float new_y = ClampRacketY(target_y);
  if (new_y == old_y) {
    return;
  }
  *racket_y = new_y;
  const int old_top = static_cast<int>(std::floor(old_y));
  const int new_top = static_cast<int>(std::floor(new_y));
  if (old_top == new_top) {
    return;
  }
  RECT rect;
  rect.left   = RacketX(cxClient, is_left);
  rect.right  = rect.left + kRacketW;
  rect.top    = (old_top < new_top) ? old_top : new_top;
  rect.bottom = ((old_top > new_top) ? old_top : new_top) + kRacketH;
  InvalidateRect(hWnd, &rect, FALSE);
}

// Reads arrow-key state and moves the player's racket. Gated on our window
// being foreground so a key held while another app is active doesn't drive
// our paddle. `dt` is real seconds since the previous tick - movement scales
// by it so the racket covers the same pixels-per-second regardless of how
// often this is called.
void TickPlayerRacket(HWND hWnd, float dt) {
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
  float* racket_y  = g_player_on_left ? &g_left_racket_y : &g_right_racket_y;
  const float step = kRacketSpeedPxPerSec * SpeedMult() * dt;
  float target     = *racket_y;
  if (up) {
    target -= step;
  }
  if (down) {
    target += step;
  }
  MoveRacket(hWnd, racket_y, g_player_on_left, target);
}

// CPU racket AI: track the ball's centre y at kMachineRacketSpeedPxPerSec,
// but aimed at where the ball *was* LagFrames() frames ago, not where it
// is now. Larger lag => the AI's aim falls behind quick angle changes,
// which is what makes Easy beatable and Hard hard. A small dead-zone
// (half a frame's movement) prevents back-and-forth jitter when the
// racket is on top of the read-out y. Tracks regardless of which way
// the ball is moving - same as the original Pong CPU.
void TickMachineRacket(HWND hWnd, float dt) {
  if (!g_ball_spawned) {
    return;
  }
  const bool machine_on_left = !g_player_on_left;
  float* racket_y            = machine_on_left ? &g_left_racket_y
                                               : &g_right_racket_y;
  // Lag lookup: TickBall writes the latest g_ball_y to g_ai_history then
  // post-increments the index, so (idx - 1) is the most recent entry and
  // (idx - 1 - LagFrames()) is the one we want. + kAiHistorySize before
  // the modulo keeps the value non-negative.
  const int lag           = LagFrames();
  const int read_idx      = (g_ai_history_idx - 1 - lag + kAiHistorySize) %
                            kAiHistorySize;
  const float lagged_y    = g_ai_history[read_idx];
  const float ball_cy     = lagged_y + 0.5f * kBallSize;
  const float racket_cy   = *racket_y + 0.5f * kRacketH;
  const float diff        = ball_cy - racket_cy;
  const float step        = kMachineRacketSpeedPxPerSec * SpeedMult() * dt;
  const float dead_zone   = 0.5f * step;
  if (diff > dead_zone) {
    MoveRacket(hWnd, racket_y, machine_on_left, *racket_y + step);
  } else if (diff < -dead_zone) {
    MoveRacket(hWnd, racket_y, machine_on_left, *racket_y - step);
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
  const float ball_speed = kBallSpeedPxPerSec * SpeedMult();
  g_ball_dx              = sx * ball_speed * std::cos(angle);
  g_ball_dy              = sy * ball_speed * std::sin(angle);
  g_ball_spawned = true;
  // Prime the AI lag history with the spawn y so the CPU has a sensible
  // value to read from even on its very first tick - otherwise the buffer
  // would still be all zeros and the AI would dive at the top of the
  // window for the first few frames.
  for (int i = 0; i < kAiHistorySize; ++i) {
    g_ai_history[i] = g_ball_y;
  }
  g_ai_history_idx = 0;
}

// Real-Pong racket bounce: the new direction depends on where on the
// paddle the ball hit, not just the incoming angle. Centre hits return
// near-horizontal; the closer to the paddle's top / bottom edge the ball
// is, the sharper the new angle (up to kMaxBounceAngle off horizontal).
//
// `ball_top_y`  is the ball's tentative new y (ny in TickBall) at hit time.
// `racket_top_y` is the paddle's current y.
// `ball_now_moves_right` is the post-bounce horizontal sign - i.e. true
//   when bouncing off the LEFT racket (ball was moving left, now moves
//   right), false when bouncing off the right.
//
// Speed magnitude is preserved across the bounce so consecutive rallies
// don't compound speed - same as the original.
void ApplyRacketBounce(float ball_top_y, float racket_top_y,
                       bool ball_now_moves_right) {
  const float ball_center_y   = ball_top_y + 0.5f * kBallSize;
  const float racket_center_y = racket_top_y + 0.5f * kRacketH;
  const float half_h          = 0.5f * kRacketH;
  // Normalised offset in [-1, 1]. +1 = bottom edge, -1 = top edge.
  // Clamped because the AABB overlap test allows the ball's centre to be
  // slightly outside the paddle if the ball is bigger than the paddle on
  // a corner clip - we don't want the angle to overshoot 90 degrees.
  float normalized = (ball_center_y - racket_center_y) / half_h;
  if (normalized < -1.0f) {
    normalized = -1.0f;
  } else if (normalized > 1.0f) {
    normalized = 1.0f;
  }
  const float angle = normalized * static_cast<float>(kMaxBounceAngle);
  const float speed =
      std::sqrt(g_ball_dx * g_ball_dx + g_ball_dy * g_ball_dy);
  const float x_sign = ball_now_moves_right ? 1.0f : -1.0f;
  g_ball_dx = x_sign * speed * std::cos(angle);
  g_ball_dy = speed * std::sin(angle);
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
  RECT rect = DisplayRectFor(cxClient, player_display);
  InvalidateRect(mainHwnd, &rect, FALSE);
}

void DrawSegmentDisplays(HDC hdc, const RECT& client) {
  const int width      = client.right - client.left;
  const RECT player_rect  = DisplayRectFor(width, /*for_player=*/true);
  const RECT machine_rect = DisplayRectFor(width, /*for_player=*/false);
  DrawOneDisplay(hdc, player_rect.left,  player_rect.top,  g_player_score);
  DrawOneDisplay(hdc, machine_rect.left, machine_rect.top, g_machine_score);
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
  for (int dash = 0; dash < kCenterLineDashCount; ++dash) {
    RECT rect;
    rect.left   = x_left;
    rect.right  = x_left + kCenterLineThickness;
    rect.top    = top + (2 * dash) * total / slots;
    rect.bottom = top + (2 * dash + 1) * total / slots;
    FillRect(hdc, &rect, hbr);
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
  RECT rect;
  rect.left   = client.left + kRacketEdgeMarginX;
  rect.right  = client.left + client_w - kRacketEdgeMarginX;
  rect.top    = client.top + kPlayfieldDividerY;
  rect.bottom = rect.top + 1;
  if (rect.right <= rect.left) {
    return;
  }
  HBRUSH hbr = CreateSolidBrush(kPlayfieldDividerColor);
  FillRect(hdc, &rect, hbr);
  DeleteObject(hbr);
}

void DrawSpawnCircle(HDC hdc, const RECT& client) {
  // Centre matches SpawnBall's choice: horizontal middle of the client,
  // vertical middle of the playfield slice (the half below kPlayfieldTopY).
  const int playfield_top    = client.top + kPlayfieldTopY;
  const int playfield_height = client.bottom - playfield_top;
  if (playfield_height <= 0) {
    return;
  }
  const int cx     = (client.left + client.right) / 2;
  const int cy     = playfield_top + playfield_height / 2;
  const int radius = kRacketH / 2;
  // Outline-only ellipse: NULL_BRUSH leaves the interior transparent, the
  // 1-px pen draws the boundary. GetStockObject brushes are owned by GDI
  // and must NOT be DeleteObject'd, so we just restore the prior selection.
  HPEN hPen     = CreatePen(PS_SOLID, 1, kSpawnCircleColor);
  HGDIOBJ oldP  = SelectObject(hdc, hPen);
  HGDIOBJ oldBr = SelectObject(hdc, GetStockObject(NULL_BRUSH));
  Ellipse(hdc, cx - radius, cy - radius, cx + radius, cy + radius);
  SelectObject(hdc, oldP);
  SelectObject(hdc, oldBr);
  DeleteObject(hPen);
}

void DrawMessageArea(HDC hdc, const RECT& client) {
  // Inner gap between each score display and the message area mirrors the
  // gap between each display and the window edge (kEdgeMarginX), so the
  // message area sits visually balanced. Height tracks kDigitH so the
  // baseline lines up with the score digits at y = kEdgeMarginY.
  RECT rect = MessageAreaRect(client.right - client.left);
  OffsetRect(&rect, client.left, client.top);
  if (rect.right <= rect.left) {
    return;
  }
  // Vertical gradient backdrop using the global g_top_color / g_bkg_color
  // pair (dark grey -> black by default; track the same colours that any
  // future full-canvas gradient will use). Painted before the frame and
  // text so both sit on top of it. We fill the full rect (including the
  // 1-px frame footprint) and let FrameRect overdraw the border below.
  FillRectWithGradient(hdc, rect, g_top_color, g_bkg_color);
  // FrameRect draws a 1-px outline using the brush's colour, leaving the
  // interior untouched - exactly what we want as a border on top of the
  // gradient backdrop.
  HBRUSH hbr = CreateSolidBrush(kMessageAreaColor);
  FrameRect(hdc, &rect, hbr);
  DeleteObject(hbr);
  if (g_message.empty()) {
    return;
  }
  // Render the message centred inside the frame in MS Sans Serif italic at
  // 1/3 the digit height. SetBkMode(TRANSPARENT) so the brush we just
  // freed isn't recreated for an opaque text background.
  HFONT hFont    = GetFont(kDigitH / 3, L"MS Sans Serif", false);
  HGDIOBJ oldFnt = SelectObject(hdc, hFont);
  const int oldBk = SetBkMode(hdc, TRANSPARENT);
  const COLORREF oldFg = SetTextColor(hdc, kMessageTextColor);
  // Pull one pixel off each side so glyphs don't draw on top of the frame.
  RECT text_rect = {rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1};
  // DT_VCENTER only works in combination with DT_SINGLELINE, so multi-line
  // messages (anything with a literal \n) need manual vertical centring:
  // measure the rendered height with DT_CALCRECT, then push the top down
  // by half the leftover space so the block sits centred in the frame.
  // DT_CENTER continues to centre each individual line horizontally.
  constexpr UINT kDrawFlags = DT_CENTER | DT_NOPREFIX;
  RECT measure_rect = text_rect;
  DrawTextW(hdc, g_message.c_str(), -1, &measure_rect, kDrawFlags | DT_CALCRECT);
  const int text_h = measure_rect.bottom - measure_rect.top;
  const int slot_h = text_rect.bottom - text_rect.top;
  if (slot_h > text_h) {
    text_rect.top += (slot_h - text_h) / 2;
  }
  DrawTextW(hdc, g_message.c_str(), -1, &text_rect, kDrawFlags);
  SetTextColor(hdc, oldFg);
  SetBkMode(hdc, oldBk);
  SelectObject(hdc, oldFnt);
  DeleteObject(hFont);
}

void SetMessage(const std::wstring& msg) {
  g_message = msg;
  if (mainHwnd == nullptr) {
    return;
  }
  RECT rect = MessageAreaRect(cxClient);
  InvalidateRect(mainHwnd, &rect, FALSE);
}

void SetPlayerOnLeft(bool on_left) {
  g_player_on_left = on_left;
}

void SetPaused(bool paused) {
  if (g_paused == paused) {
    return;
  }
  g_paused = paused;
  // Music pauses/resumes with the game. SyncBgm reads g_sound_on &&
  // !g_paused to decide; the MCI device stays open across pauses so
  // resuming is just "MCI resume" (no re-open, no restart).
  SyncBgm();
}

void SetSoundOn(bool on) {
  if (g_sound_on == on) {
    return;
  }
  g_sound_on = on;
  // SyncBgm starts MCI on flip-up, pauses on flip-down. Hit sounds gate
  // themselves on g_sound_on inside PlayHit so they don't need anything
  // more here.
  SyncBgm();
}

void SetSpeed(Speed speed) {
  if (speed == g_speed) {
    return;
  }
  // Racket and (future) ball-spawn calls compute step * SpeedMult() each
  // frame so they pick up the new value automatically. The *in-flight*
  // ball has a velocity baked in from the previous spawn though, so we
  // rescale (dx, dy) by the ratio so a mid-game speed change is felt
  // immediately instead of only on the next spawn.
  const float old_mult = SpeedMult();
  g_speed              = speed;
  if (g_ball_spawned && old_mult != 0.0f) {
    const float ratio = SpeedMult() / old_mult;
    g_ball_dx *= ratio;
    g_ball_dy *= ratio;
  }
}

void SetDifficulty(Difficulty difficulty) {
  // Difficulty only feeds into TickMachineRacket via LagFrames(), which
  // is read each frame against g_ai_history - no per-instance state to
  // rescale.
  g_difficulty = difficulty;
}

void ResetForNewGame(HWND hWnd) {
  if (hWnd == nullptr) {
    return;
  }
  // Centre the paddles and re-launch the ball at centre with a fresh angle.
  // SpawnBall installs a (dx, dy) here, but the ball won't actually move
  // until g_running flips true - that's the whole point of stopping at the
  // "new game" banner: positions reset and ready to go on the next F3.
  CenterRackets();
  SpawnBall();
  // Zero the scoreboards. UpdateSegmentDisplay is the canonical way to
  // change a score; it both stores and invalidates the matching display.
  UpdateSegmentDisplay(/*player_display=*/true,  0);
  UpdateSegmentDisplay(/*player_display=*/false, 0);
  // Belt-and-braces full repaint - the paddles, ball, and any in-flight
  // dirty regions all snap back to the reset state in a single frame.
  InvalidateRect(hWnd, nullptr, FALSE);
}

void InitRackets(HWND hWnd) {
  if (hWnd == nullptr) {
    return;
  }
  g_left_racket_y  = -1.0f;
  g_right_racket_y = -1.0f;
  // Try to centre now in case WM_SIZE has already run; if not, TickRackets
  // will do it on the first tick that sees a non-zero cyClient.
  CenterRackets();
  InvalidateRect(hWnd, nullptr, FALSE);
}

void TickRackets(HWND hWnd, float dt) {
  if (hWnd == nullptr) {
    return;
  }
  // Lazy-centre on the first tick that has a real cyClient. We invalidate
  // and skip the input/AI step so the rackets appear immediately on this
  // frame and start tracking next frame - simpler than threading state for
  // "just centred".
  if (g_left_racket_y < 0.0f || g_right_racket_y < 0.0f) {
    if (cyClient <= 0) {
      return;
    }
    CenterRackets();
    InvalidateRect(hWnd, nullptr, FALSE);
    return;
  }
  if (!g_running || g_paused) {
    return;
  }
  TickPlayerRacket(hWnd, dt);
  TickMachineRacket(hWnd, dt);
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
  g_ball_x       = 0.0f;
  g_ball_y       = 0.0f;
  g_ball_dx      = 0.0f;
  g_ball_dy      = 0.0f;
  // Try to spawn now in case cxClient/cyClient are already known; otherwise
  // TickBall will retry every frame until WM_SIZE has fired.
  SpawnBall();
  InvalidateRect(hWnd, nullptr, FALSE);
}

float NextFrameDelta() {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  if (!g_qpc_initialized) {
    // First call - establish the baseline. No previous frame, so the delta
    // is zero; callers will see no movement this tick (which is fine,
    // since the first tick is reserved for lazy-init anyway).
    QueryPerformanceFrequency(&g_qpc_freq);
    g_qpc_last        = now;
    g_qpc_initialized = true;
    return 0.0f;
  }
  const long long ticks =
      static_cast<long long>(now.QuadPart - g_qpc_last.QuadPart);
  g_qpc_last = now;
  if (g_qpc_freq.QuadPart <= 0) {
    return 0.0f;
  }
  float dt =
      static_cast<float>(ticks) / static_cast<float>(g_qpc_freq.QuadPart);
  if (dt < 0.0f) {
    dt = 0.0f;
  } else if (dt > kMaxDeltaSeconds) {
    dt = kMaxDeltaSeconds;
  }
  return dt;
}

void CenterBallAtSpawn() {
  if (!g_ball_spawned || cxClient <= 0 || cyClient <= 0) {
    return;
  }
  // Same formula as SpawnBall, but no velocity / spawn-flag changes - we
  // just want the resting ball to track the playfield centre as the window
  // is resized.
  g_ball_x = 0.5f * cxClient - 0.5f * kBallSize;
  g_ball_y = kPlayfieldTopY + 0.5f * (cyClient - kPlayfieldTopY) -
             0.5f * kBallSize;
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
  const float playfield_h = static_cast<float>(cyClient - kPlayfieldTopY);
  const float centered    = kPlayfieldTopY + 0.5f * (playfield_h - kRacketH);
  g_left_racket_y         = centered;
  g_right_racket_y        = centered;
}

void TickBall(HWND hWnd, float dt) {
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
  if (!g_running || g_paused) {
    return;
  }
  const float old_x = g_ball_x;
  const float old_y = g_ball_y;
  // dx / dy are px/sec; scale by real elapsed seconds so motion is timer-
  // rate independent. Long stalls are clamped via kMaxDeltaSeconds upstream
  // in NextFrameDelta - that prevents teleporting through paddles.
  float nx = old_x + g_ball_dx * dt;
  float ny = old_y + g_ball_dy * dt;

  // Top / bottom bounce. Reflect the overshoot back into the playfield
  // (nx' = 2*wall - nx) instead of clamping, so the ball preserves its
  // sub-frame momentum and bouncing speed stays consistent.
  if (ny < kPlayfieldTopY) {
    ny        = 2.0f * kPlayfieldTopY - ny;
    g_ball_dy = -g_ball_dy;
    PlayHit(IDR_WALL_WAV);
  } else if (ny + kBallSize > cyClient) {
    ny        = 2.0f * (cyClient - kBallSize) - ny;
    g_ball_dy = -g_ball_dy;
    PlayHit(IDR_WALL_WAV);
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
      nx = 2.0f * lr_right - nx;
      // Real-Pong-style angle bounce: where the ball hit on the paddle
      // sets the new vertical component, not the incoming dy. After this
      // call the ball moves right.
      ApplyRacketBounce(ny, g_left_racket_y, /*ball_now_moves_right=*/true);
      PlayHit(IDR_RACKET_WAV);
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
      nx = 2.0f * (rr_left - kBallSize) - nx;
      ApplyRacketBounce(ny, g_right_racket_y, /*ball_now_moves_right=*/false);
      PlayHit(IDR_RACKET_WAV);
    }
  }

  if (nx == old_x && ny == old_y) {
    return;
  }
  g_ball_x = nx;
  g_ball_y = ny;
  // Feed the AI's prediction-lag history. Writing the post-bounce y
  // (rather than the tentative pre-bounce one) means the CPU sees a
  // continuous trajectory across reflections, not phantom out-of-bounds
  // jumps.
  g_ai_history[g_ai_history_idx] = g_ball_y;
  g_ai_history_idx               = (g_ai_history_idx + 1) % kAiHistorySize;

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
  RECT rect;
  rect.left   = static_cast<int>(std::floor(min_x));
  rect.top    = static_cast<int>(std::floor(min_y));
  rect.right  = static_cast<int>(std::ceil(max_x + kBallSize));
  rect.bottom = static_cast<int>(std::ceil(max_y + kBallSize));
  InvalidateRect(hWnd, &rect, FALSE);
}

void DrawBall(HDC hdc, const RECT& client) {
  (void)client;
  if (!g_ball_spawned) {
    return;
  }
  const int left_px = static_cast<int>(std::floor(g_ball_x));
  const int top_px  = static_cast<int>(std::floor(g_ball_y));
  RECT rect;
  rect.left   = left_px;
  rect.top    = top_px;
  rect.right  = left_px + kBallSize;
  rect.bottom = top_px + kBallSize;
  HBRUSH hbr = CreateSolidBrush(kBallColor);
  FillRect(hdc, &rect, hbr);
  DeleteObject(hbr);
}

bool ConfirmNewGame(HWND hWnd) {
  const int new_game_dialog =
      MessageBoxW(hWnd, L"Are you sure you want to start a new game?",
                  L"Confirm New Game", MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1);
  return new_game_dialog == IDYES;
}

// Confirmation dialog for exit
bool ConfirmExit(HWND hWnd) {
  const int exit_dialog =
      MessageBoxW(hWnd, L"Are you sure you want to Exit?",
                  L"Confirm Exit Game", MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2);
  return exit_dialog == IDYES;
}
