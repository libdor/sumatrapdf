/* Copyright 2018 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "utils/BaseUtil.h"
#include "LabelWithCloseWnd.h"
#include "utils/Dpi.h"
#include "utils/GdiPlusUtil.h"
#include "utils/WinUtil.h"

#define CLOSE_BTN_DX 16
#define CLOSE_BTN_DY 16
#define LABEL_BUTTON_SPACE_DX 8

#define WND_CLASS_NAME L"LabelWithCloseWndClass"

static bool IsMouseOverClose(LabelWithCloseWnd* w) {
    PointI p;
    GetCursorPosInHwnd(w->hwnd, p);
    return w->closeBtnPos.Contains(p);
}

// Draws the 'x' close button in regular state or onhover state
// Tries to mimic visual style of Chrome tab close button
static void DrawCloseButton(HDC hdc, LabelWithCloseWnd* w) {
    Graphics g(hdc);
    g.SetCompositingQuality(CompositingQualityHighQuality);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPageUnit(UnitPixel);
    // GDI+ doesn't pick up the window's orientation through the device context,
    // so we have to explicitly mirror all rendering horizontally
    if (IsRtl(w->hwnd)) {
        g.ScaleTransform(-1, 1);
        g.TranslateTransform((REAL)ClientRect(w->hwnd).dx, 0, MatrixOrderAppend);
    }

    Color c;
    RectI& r = w->closeBtnPos;

    // in onhover state, background is a red-ish circle
    bool onHover = IsMouseOverClose(w);
    if (onHover) {
        c.SetFromCOLORREF(COL_CLOSE_HOVER_BG);
        SolidBrush b(c);
        g.FillEllipse(&b, r.x, r.y, r.dx - 2, r.dy - 2);
    }

    // draw 'x'
    c.SetFromCOLORREF(onHover ? COL_CLOSE_X_HOVER : COL_CLOSE_X);
    g.TranslateTransform((float)r.x, (float)r.y);
    Pen p(c, 2);
    if (onHover) {
        g.DrawLine(&p, Point(4, 4), Point(r.dx - 6, r.dy - 6));
        g.DrawLine(&p, Point(r.dx - 6, 4), Point(4, r.dy - 6));
    } else {
        g.DrawLine(&p, Point(4, 5), Point(r.dx - 6, r.dy - 5));
        g.DrawLine(&p, Point(r.dx - 6, 5), Point(4, r.dy - 5));
    }
}

static void PaintHDC(LabelWithCloseWnd* w, HDC hdc, const PAINTSTRUCT& ps) {
    HBRUSH br = CreateSolidBrush(w->bgCol);
    FillRect(hdc, &ps.rcPaint, br);

    ClientRect cr(w->hwnd);

    int x = DpiScaleX(w->hwnd, w->padX);
    int y = DpiScaleY(w->hwnd, w->padY);
    UINT opts = ETO_OPAQUE;
    if (IsRtl(w->hwnd)) {
        opts = opts | ETO_RTLREADING;
    }

    HGDIOBJ prevFont = nullptr;
    if (w->font) {
        prevFont = SelectObject(hdc, w->font);
    }
    SetTextColor(hdc, w->txtCol);
    SetBkColor(hdc, w->bgCol);

    WCHAR* s = win::GetText(w->hwnd);
    ExtTextOut(hdc, x, y, opts, nullptr, s, (UINT)str::Len(s), nullptr);
    free(s);

    // Text might be too long and invade close button area. We just re-paint
    // the background, which is not the pretties but works.
    // A better way would be to intelligently truncate text or shrink the font
    // size (within reason)
    x = w->closeBtnPos.x - DpiScaleX(w->hwnd, LABEL_BUTTON_SPACE_DX);
    RectI ri(x, 0, cr.dx - x, cr.dy);
    RECT r = ri.ToRECT();
    FillRect(hdc, &r, br);

    DrawCloseButton(hdc, w);
    DeleteObject(br);

    if (w->font) {
        SelectObject(hdc, prevFont);
    }
}

static void OnPaint(LabelWithCloseWnd* w) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(w->hwnd, &ps);
    DoubleBuffer buffer(w->hwnd, RectI::FromRECT(ps.rcPaint));
    PaintHDC(w, buffer.GetDC(), ps);
    buffer.Flush(hdc);
    EndPaint(w->hwnd, &ps);
}

static void CalcCloseButtonPos(LabelWithCloseWnd* w, int dx, int dy) {
    int btnDx = DpiScaleX(w->hwnd, CLOSE_BTN_DX);
    int btnDy = DpiScaleY(w->hwnd, CLOSE_BTN_DY);
    int x = dx - btnDx - DpiScaleX(w->hwnd, w->padX);
    int y = 0;
    if (dy > btnDy) {
        y = (dy - btnDy) / 2;
    }
    w->closeBtnPos = RectI(x, y, btnDx, btnDy);
}

static LRESULT CALLBACK WndProcLabelWithClose(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (WM_ERASEBKGND == msg) {
        return TRUE; // tells Windows we handle background erasing so it doesn't do it
    }

    LabelWithCloseWnd* w = nullptr;
    if (WM_NCCREATE == msg) {
        LPCREATESTRUCT lpcs = reinterpret_cast<LPCREATESTRUCT>(lp);
        w = reinterpret_cast<LabelWithCloseWnd*>(lpcs->lpCreateParams);
        w->hwnd = hwnd;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LPARAM>(w));
        goto DoDefault;
    } else {
        w = reinterpret_cast<LabelWithCloseWnd*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (!w) {
        goto DoDefault;
    }

    // to match other controls, preferred way is explict SetFont() call
    if (WM_SETFONT == msg) {
        w->SetFont((HFONT)wp);
        return 0;
    }

    if (WM_GETFONT == msg) {
        return (LRESULT)w->font;
    }

    if (WM_SIZE == msg) {
        int dx = LOWORD(lp);
        int dy = HIWORD(lp);
        CalcCloseButtonPos(w, dx, dy);
        ScheduleRepaint(hwnd);
        return 0;
    }

    if (WM_MOUSEMOVE == msg) {
        ScheduleRepaint(w->hwnd);

        if (IsMouseOverClose(w)) {
            TrackMouseLeave(hwnd);
        }
        goto DoDefault;
    }

    if (WM_MOUSELEAVE == msg) {
        ScheduleRepaint(w->hwnd);
        return 0;
    }

    if (WM_LBUTTONUP == msg) {
        if (IsMouseOverClose(w)) {
            HWND parent = GetParent(w->hwnd);
            SendMessage(parent, WM_COMMAND, w->cmd, 0);
        }
        return 0;
    }

    if (WM_PAINT == msg) {
        OnPaint(w);
        return 0;
    }

DoDefault:
    return DefWindowProc(hwnd, msg, wp, lp);
}

static void RegisterLabelWithCloseWnd() {
    static ATOM atom = 0;

    if (atom != 0) {
        // already registered
        return;
    }

    WNDCLASSEX wcex = {};
    FillWndClassEx(wcex, WND_CLASS_NAME, WndProcLabelWithClose);
    atom = RegisterClassExW(&wcex);
    CrashIf(!atom);
}

void LabelWithCloseWnd::SetLabel(const WCHAR* label) {
    win::SetText(this->hwnd, label);
    ScheduleRepaint(this->hwnd);
}

void LabelWithCloseWnd::SetBgCol(COLORREF c) {
    this->bgCol = c;
    ScheduleRepaint(this->hwnd);
}

void LabelWithCloseWnd::SetTextCol(COLORREF c) {
    this->txtCol = c;
    ScheduleRepaint(this->hwnd);
}

// cmd is both the id of the window as well as id of WM_COMMAND sent
// when close button is clicked
// caller needs to free() the result
bool LabelWithCloseWnd::Create(HWND parent, int cmd) {
    RegisterLabelWithCloseWnd();

    this->cmd = cmd;
    this->bgCol = GetSysColor(COLOR_BTNFACE);
    this->txtCol = GetSysColor(COLOR_BTNTEXT);

    // sets w->hwnd during WM_NCCREATE
    HWND hwnd = CreateWindow(WND_CLASS_NAME, L"", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, parent, (HMENU)(INT_PTR)cmd,
                             GetModuleHandle(nullptr), this);
    CrashIf(this->hwnd != hwnd);
    CrashIf(!this->hwnd);
    return this->hwnd != nullptr;
}

SizeI LabelWithCloseWnd::GetIdealSize() {
    WCHAR* s = win::GetText(this->hwnd);
    SizeI size = TextSizeInHwnd(this->hwnd, s);
    free(s);
    int btnDx = DpiScaleX(this->hwnd, CLOSE_BTN_DX);
    int btnDy = DpiScaleY(this->hwnd, CLOSE_BTN_DY);
    size.dx += btnDx;
    size.dx += DpiScaleX(this->hwnd, LABEL_BUTTON_SPACE_DX);
    size.dx += 2 * DpiScaleX(this->hwnd, this->padX);
    if (size.dy < btnDy) {
        size.dy = btnDy;
    }
    size.dy += 2 * DpiScaleY(this->hwnd, this->padY);
    return size;
}

void LabelWithCloseWnd::SetFont(HFONT f) {
    this->font = f;
    // TODO: if created, set on the label?
}

void LabelWithCloseWnd::SetPaddingXY(int x, int y) {
    this->padX = x;
    this->padY = y;
    ScheduleRepaint(this->hwnd);
}
