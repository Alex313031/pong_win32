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

#define IDM_NEWGAME                 203
#define IDM_PAUSE                   204
#define IDM_SPEED_LOW               205
#define IDM_SPEED_MED               206
#define IDM_SPEED_HIGH              207
#define IDM_SOUND                   208
#define IDM_SAVEAS                  209

// Timers
#define TIMER_GAME                  300 // Timer ID for game logic/painting

// Custom posted-message IDs (WM_APP range, guaranteed to not clash with any
// system / common-control message). Used to defer work that mustn't run
// inside WM_CREATE — see WM_APP_AUTOPLAY usage in main.cc.
#define WM_APP_AUTOPLAY             (WM_APP + 0)

// For resources to be loaded without an ID from the system.
#ifndef IDC_STATIC
 #define IDC_STATIC                 -1
#endif // IDC_STATIC
// clang-format on
