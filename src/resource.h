// {{NO_DEPENDENCIES}}
// For #define-ing static resources for resource script file(s).
// Used by resource.rc

// clang-format off

/* Icons */
#define IDI_MAIN                    101 /* 32x32 & 48x48 icon */
#define IDI_SMALL                   102 /* Small 16x16 icon */
#define IDI_ABOUT                   103 /* About Dialog icon */

/* Main application resource, also used to attach menu */
#define IDR_MAIN                    120

/* Dialogs */
#define IDD_ABOUTDLG                130 // About Dialog

/* Menu items */
#define IDM_ABOUT                   200
#define IDM_EXIT                    201
#define IDM_HELP                    202
#define IDM_SAVEAS                  203

#define IDM_NEWGAME                 204
#define IDM_PAUSE                   205
#define IDM_SOUND                   206
#define IDM_RESET                   207
#define IDM_PLAYER                  208
#define IDM_HUMAN                   209

#define IDM_SPEED_LOW               210
#define IDM_SPEED_MED               211
#define IDM_SPEED_HIGH              212

#define IDM_EASY                    213
#define IDM_MED                     214
#define IDM_HARD                    215

// Timers
#define TIMER_GAME                  300 // Timer ID for game logic/painting

// Embedded background-music WAV. Loaded as a user-defined "WAVE" resource
// when kUseEmbeddedBgm is true (see utils.h). The RC file binds this ID
// to res/music.wav; FindResourceW(L"WAVE") picks it up at runtime.
#define IDR_BGM_WAV                 500

#define IDR_RACKET_WAV              501 // Racket ball bounce sound
#define IDR_WALL_WAV                502 // Wall ball bounce sound

// Custom posted-message IDs (WM_APP range, guaranteed to not clash with any
// system / common-control message). Used to defer work that mustn't run
// inside WM_CREATE — see WM_APP_AUTOPLAY usage in main.cc.
#define WM_APP_AUTOPLAY             (WM_APP + 0)

// For resources to be loaded without an ID from the system.
#ifndef IDC_STATIC
 #define IDC_STATIC                 -1
#endif // IDC_STATIC
// clang-format on
