#ifndef PONGWIN32_GAME_H_
#define PONGWIN32_GAME_H_

#include "framework.h"

// Max number of points that can be kept track of in one of the 7 segment display areas.
static inline constexpr unsigned int kMaxNumPoints = 999u;

// Initializes the two 7 segment display areas, in top left and right of the client
// area of hWnd. Each display has 3 7 segment numerals, allowing up to 999 points.
// Resets both scores to zero and invalidates the window so the displays appear
// on the next WM_PAINT.
bool InitSegmentDisplays(HWND hWnd);

// Updates either the player's segment display (player_display == true, top-left)
// or the machine's (player_display == false, top-right). `score` is clamped to
// kMaxNumPoints. Invalidates only the affected display rect so we don't repaint
// the whole client area for a single score change.
void UpdateSegmentDisplay(bool player_display, unsigned int score);

// Paints both segment displays onto hdc. Called from WM_PAINT after the
// background fill so the digits sit on top of g_bkg_color. `client` is the
// full client rect; the display positions are derived from its width.
void DrawSegmentDisplays(HDC hdc, const RECT& client);

// Paints the vertical center "court" line onto hdc. Called from WM_PAINT
// after the background fill. Position derives from `client`'s midpoint.
void DrawCenterLine(HDC hdc, const RECT& client);

// Paints the 1-px horizontal divider that marks the top of the playfield
// (kPlayfieldTopY). Spans from the left edge of the left score display to
// the right edge of the right one, not all the way across the client.
void DrawPlayfieldDivider(HDC hdc, const RECT& client);

// Paints the 1-px frame around the state-message area between the two
// score displays. Height matches the displays; horizontal padding to each
// display matches the displays' padding to the window edge.
void DrawMessageArea(HDC hdc, const RECT& client);

// Initializes the two paddle "rackets" - one on each side of the client
// area - and centers them vertically. Called from InitApp; the actual
// centering happens lazily on the first WM_TIMER tick where cyClient is
// known, since WM_CREATE arrives before WM_SIZE.
void InitRackets(HWND hWnd);

// Called from WM_TIMER. Polls the arrow keys via GetAsyncKeyState and
// moves the player's racket up/down accordingly. The CPU's racket sits
// still until game logic lands. Up/Left = up, Down/Right = down. Only
// reads input when our window is foreground so other apps don't drive
// the paddle.
void TickRackets(HWND hWnd);

// Paints both rackets onto hdc. Called from WM_PAINT after the background
// fill / center line / segment displays so rackets sit on top of every
// other element.
void DrawRackets(HDC hdc, const RECT& client);

// Sets whether the player controls the left racket (true) or the right
// (false). Defaults to true. Re-call when wiring a menu choice later.
void SetPlayerOnLeft(bool on_left);

// Initializes the ball state. Spawn (centre + random horizontal direction)
// happens lazily on the first WM_TIMER tick where cxClient/cyClient are
// known, for the same WM_CREATE-before-WM_SIZE reason as InitRackets.
void InitBall(HWND hWnd);

// Called from WM_TIMER. Advances the ball one step in its current direction
// and invalidates just the union of its old and new rect.
void TickBall(HWND hWnd);

// Paints the ball onto hdc. Called from WM_PAINT after the rackets.
void DrawBall(HDC hdc, const RECT& client);

#endif // PONGWIN32_GAME_H_
