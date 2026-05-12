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

// Court center line. White on g_bkg_color, drawn vertically through the
// midpoint of the client area. kCenterLineMarginY is the gap left at the
// top and bottom of the client so the line doesn't run all the way to the
// chrome / status bar edges; kCenterLineThickness is its width.
constexpr COLORREF kCenterLineColor = RGB(255, 255, 255);
constexpr int kCenterLineMarginY    = 14;
constexpr int kCenterLineThickness  = 6;

// Live score state. File-scope so InitSegmentDisplays / UpdateSegmentDisplay
// can mutate without exposing the variables to the rest of the program -
// callers go through the API, the renderer goes through DrawSegmentDisplays.
unsigned int g_player_score  = 0;
unsigned int g_machine_score = 0;

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
  // client - keeps a 30 fps tick of score updates from forcing a full
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
  r.top    = client.top + kCenterLineMarginY;
  r.bottom = client.bottom - kCenterLineMarginY;
  HBRUSH hbr = CreateSolidBrush(kCenterLineColor);
  FillRect(hdc, &r, hbr);
  DeleteObject(hbr);
}
