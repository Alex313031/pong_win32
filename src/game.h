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

// Paints the 1-px yellow guide circle at the ball's spawn point. Should
// be drawn before the center line, rackets, and ball so they all overdraw
// it - the circle reveals itself wherever none of those are covering it.
void DrawSpawnCircle(HDC hdc, const RECT& client);

// Paints the 1-px frame around the state-message area between the two
// score displays. Height matches the displays; horizontal padding to each
// display matches the displays' padding to the window edge. Also renders
// whatever string SetMessage last set, centered inside the frame.
void DrawMessageArea(HDC hdc, const RECT& client);

// Replaces the text shown in the message area. Pass an empty wstring to
// leave the frame empty. Invalidates just the message area's rect so the
// per-tick repaint cost stays bounded.
void SetMessage(const std::wstring& msg);

// Initializes the two paddle "rackets" - one on each side of the client
// area - and centers them vertically. Called from InitApp; the actual
// centering happens lazily on the first WM_TIMER tick where cyClient is
// known, since WM_CREATE arrives before WM_SIZE.
void InitRackets(HWND hWnd);

// Called from WM_TIMER. Polls the arrow keys via GetAsyncKeyState and
// moves the player's racket up/down accordingly. The CPU's racket sits
// still until game logic lands. Up/Left = up, Down/Right = down. Only
// reads input when our window is foreground so other apps don't drive
// the paddle. `dt` is the real elapsed time since the previous tick in
// seconds, used to scale movement so game speed doesn't depend on the
// timer's accuracy or rate.
void TickRackets(HWND hWnd, float dt);

// Paints both rackets onto hdc. Called from WM_PAINT after the background
// fill / center line / segment displays so rackets sit on top of every
// other element.
void DrawRackets(HDC hdc, const RECT& client);

// Sets whether the player controls the left racket (true) or the right
// (false). Defaults are read from the IDM_PLAYER menu's CHECKED state at
// startup (see ApplyMenuDefaults in main.cc).
void SetPlayerOnLeft(bool on_left);

// Pause toggle. When true, TickRackets and TickBall skip all movement /
// input / AI - the window keeps repainting current state but nothing
// advances. Lazy spawn / centre still runs so the field appears even if
// the game starts paused. Default comes from IDM_PAUSE's CHECKED state.
void SetPaused(bool paused);

// Sound toggle. When false, the hit-beep helpers in game.cc short-circuit
// so paddle / wall bounces fall silent. Default comes from IDM_SOUND's
// CHECKED state in the .rc.
void SetSoundOn(bool on);

// Game-wide speed setting. Med == the *PxPerSec constants unchanged;
// Low drops everything by a third, High raises it by a third. Affects
// ball, player racket, and machine racket together. Setting a new value
// rescales the in-flight ball's velocity so a mid-game speed change
// takes effect immediately rather than only on the next spawn.
enum class Speed { Low, Med, High };
void SetSpeed(Speed speed);

// CPU difficulty setting. The machine racket always moves at
// kMachineRacketSpeedPxPerSec (scaled by Speed); difficulty controls a
// reaction-lag instead - the AI aims at the ball's y from N frames ago
// rather than its current y. Easy = ~200 ms lag, Med = ~100 ms,
// Hard = ~50 ms (see kAiLagFrames* in constants.h). That matches the
// original Pong AI's beatability profile - perfect tracking but
// limited by speed and a small reaction delay - rather than just
// making the CPU faster on harder settings.
enum class Difficulty { Easy, Med, Hard };
void SetDifficulty(Difficulty difficulty);

// Resets the playfield to its "new match" state: rackets centred, ball
// re-spawned at centre with a fresh random launch vector, scores zeroed.
// The ball won't actually move until g_running flips true (typically via
// the IDM_PAUSE / F3 handler in main.cc).
void ResetForNewGame(HWND hWnd);

// Initializes the ball state. Spawn (centre + random horizontal direction)
// happens lazily on the first WM_TIMER tick where cxClient/cyClient are
// known, for the same WM_CREATE-before-WM_SIZE reason as InitRackets.
void InitBall(HWND hWnd);

// Snaps the ball back to the playfield's centre point (the same spot
// SpawnBall picks), without disturbing its current velocity vector. Used
// from WM_SIZE while the game is stopped so the resting ball tracks the
// recentred playfield instead of drifting off relative to the new size.
void CenterBallAtSpawn();

// Snaps both rackets back to the vertical centre of the playfield. Same
// resize-tracking purpose as CenterBallAtSpawn, but for the paddles.
void CenterRackets();

// Called from WM_TIMER. Advances the ball by velocity * dt seconds in its
// current direction and invalidates just the union of its old and new rect.
void TickBall(HWND hWnd, float dt);

// Computes the elapsed wall-clock time (in seconds) since the previous
// call. First call returns 0 (no previous baseline). Backed by
// QueryPerformanceCounter, so much higher resolution than WM_TIMER itself.
// Clamped to a sane upper bound so a long stall (debugger break, OS
// scheduler hiccup) doesn't teleport game objects across the field.
// Intended usage: call once per WM_TIMER, pass the result to both Tick*.
float NextFrameDelta();

// Paints the ball onto hdc. Called from WM_PAINT after the rackets.
void DrawBall(HDC hdc, const RECT& client);

// Confirmation dialog for new game
bool ConfirmNewGame(HWND hWnd);

// Confirmation dialog for exit
bool ConfirmExit(HWND hWnd);

#endif // PONGWIN32_GAME_H_
