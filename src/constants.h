#ifndef PONGWIN32_CONSTANTS_H_
#define PONGWIN32_CONSTANTS_H_

#include "framework.h"

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

// WM_TIMER delay, set to approx ~60 FPS
inline constexpr UINT kGameTickDelay = static_cast<UINT>(std::round(16.67f));

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

// Tunable constants for the rendering, physics, and audio paths. Pulled out
// of game.cc so anyone can adjust colours, sizes, and speeds in one place
// without touching the game logic. Each block of constants documents what
// it controls.

// ---------------------------------------------------------------------------
// 7-segment LED score displays.
//
// Classic LED look: bright red lit segment over a dim red unlit ghost so the
// digit silhouette is always visible (this is what Win 3.1/95 Minesweeper and
// most pocket calculators do). Painting unlit segments instead of leaving
// them as bare background also means we don't need a separate "8 8 8"
// backdrop pass.
constexpr COLORREF kSegOn  = RGB(255, 0, 0);
constexpr COLORREF kSegOff = RGB(50, 0, 0);

// Per-digit segment bitmasks. Bit layout: 0=a 1=b 2=c 3=d 4=e 5=f 6=g,
// matching the conventional 7-seg labeling:
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
constexpr int kDigitW = 24;
constexpr int kDigitH = 48;
constexpr int kSegT   = 6;
// Perpendicular inset at each segment-to-segment join. The classic LED look
// has a visible hairline between adjacent segments rather than them merging
// into a single blob; pulling each segment's slanted edges inward by this
// many pixels along their axis produces that hairline. Since every join is
// at 45 degrees, axial offset g yields g*sqrt(2) px perpendicular - so 2
// here is ~2.8 px visible gap.
constexpr int kSegGap   = 2;
constexpr int kDigitGap = 5;
// X/Y margins from the window edges to the display. Split so the displays
// can be pushed further inward horizontally (clear of the paddle columns)
// without also pushing them down away from the top edge, and vice versa.
constexpr int kEdgeMarginX = 48;
constexpr int kEdgeMarginY = 14;
constexpr int kDisplayW    = kDigitW * 3 + kDigitGap * 2;

// ---------------------------------------------------------------------------
// Playfield divider.
//
// kPlayfieldDividerY is the y of the 1-px horizontal divider that visually
// separates the score / state-message strip from the playfield below it.
// kPlayfieldTopY sits one row beneath it and is what the center line,
// rackets, and ball all clamp / bounce against, so the divider always
// stays untouched on top - no center-line dash or paddle ever overdraws it.
constexpr int kPlayfieldDividerY          = kEdgeMarginY + kDigitH + kEdgeMarginY;
constexpr COLORREF kPlayfieldDividerColor = RGB_LTGREY;
constexpr int kPlayfieldTopY              = kPlayfieldDividerY + 1;

// ---------------------------------------------------------------------------
// Message area.
//
// 1-px frame between the two score displays, same height as the digits,
// used to render state-message text (READY, PAUSED, etc.). Inset from each
// display by kEdgeMarginX so it sits visually balanced between them at the
// same x-padding the displays use on the outside. Text is the default UI
// font, italic, glyph height kDigitH / 2, drawn centred both ways inside
// the frame.
constexpr COLORREF kMessageAreaColor = RGB_LTGREY;
constexpr COLORREF kMessageTextColor = RGB_LTGREY;

// ---------------------------------------------------------------------------
// Court center line.
//
// White dashes on g_bkg_color, drawn vertically through the midpoint of the
// client area. The top is clamped to kPlayfieldTopY so it can't intrude on
// the score / state-message strip; kCenterLineMarginY is the gap left at
// the bottom of the client so the line doesn't run all the way to the
// chrome / status bar edge. kCenterLineThickness is the dash width.
// kCenterLineDashCount is the *fixed* number of dashes - dash and gap
// heights are derived from the available vertical space at draw time so a
// resize keeps the count constant and just stretches the spacing.
constexpr COLORREF kCenterLineColor = RGB_LTGREY;
constexpr int kCenterLineMarginY    = 0;
constexpr int kCenterLineThickness  = 3;
constexpr int kCenterLineDashCount  = 22;

// ---------------------------------------------------------------------------
// Rackets.
//
// Two white rectangles, one anchored at each side. kRacketEdgeMarginX is
// the gap from the window edge to the racket's outer side. The two
// *PxPerSec constants are real-world velocities (pixels per second) - the
// tick handlers multiply by the per-frame dt so movement speed is decoupled
// from the timer rate. Splitting player/machine lets us nerf or buff the
// AI without touching the player's responsiveness.
// Player rackets are green, machine rackets are blue - colour follows the
// role, not the physical side, so toggling g_player_on_left swaps which
// side is green and which is blue.
constexpr COLORREF kPlayerRacketColor       = RGB_GREEN;
constexpr COLORREF kMachineRacketColor      = RGB_BLUE;
constexpr int kRacketW                      = 14;
constexpr int kRacketH                      = 80;
constexpr int kRacketEdgeMarginX            = 18;
constexpr float kRacketSpeedPxPerSec        = 400.0f;
constexpr float kMachineRacketSpeedPxPerSec = 200.0f;

// ---------------------------------------------------------------------------
// Spawn circle.
//
// Yellow 1-px outline at the ball's spawn point, diameter == kRacketH.
// Painted before the center line and rackets / ball so they all overdraw
// it - the circle is only visible while no other element is directly over
// it.
constexpr COLORREF kSpawnCircleColor = RGB_YELLOW;

// ---------------------------------------------------------------------------
// Ball.
//
// Square, white, kBallSize on a side. kBallSpeedPxPerSec is the magnitude
// of the velocity vector in pixels per second; (dx, dy) decomposes it via
// cos/sin so the ball travels at the same speed regardless of launch angle.
// TickBall scales by per-frame dt so timer jitter doesn't translate into
// position jitter.
constexpr COLORREF kBallColor      = RGB(255, 255, 255);
constexpr int kBallSize            = 14;
constexpr float kBallSpeedPxPerSec = 500.0f;

// Launch angle bound. The ball comes off centre at a uniformly random angle
// between 0 and this value off the horizontal axis, with independent coin
// flips picking left/right and up/down. 45 keeps the motion noticeably
// horizontal-dominant - the original Pong feel - while still giving the
// player enough vertical surprise to make tracking it interesting.
constexpr double kPi             = 3.14159265358979323846;
constexpr double kMaxLaunchAngle = kPi / 4.0;

// ---------------------------------------------------------------------------
// Audio cues.
//
// Hit sounds are short MS-ADPCM wavs bundled as WAVE resources
// (IDR_RACKET_WAV / IDR_WALL_WAV - see resource.h and the assets/ build
// scripts) and played asynchronously through PlaySoundW. The frequency
// choice (B♭5 for paddles, B♭4 for walls - exactly one octave apart) lives
// in assets/make_sine_wav.py since that's where the wavs are generated;
// changing the frequencies means re-running the build script, not editing
// this header. Gated on g_sound_on so the IDM_SOUND menu item can mute the
// game without removing the calls.

// ---------------------------------------------------------------------------
// Frame timing.
//
// Per-tick dt is clamped to this. If the app stalls for a long beat
// (debugger break, OS scheduler hiccup), we don't want the ball to
// teleport across the field on the next frame.
constexpr float kMaxDeltaSeconds = 0.1f;

#endif // PONGWIN32_CONSTANTS_H_
