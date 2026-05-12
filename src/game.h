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

#endif // PONGWIN32_GAME_H_
