#include "gui_controller.h"

#include <windowsx.h>
#include <dwmapi.h>
#include <gdiplus.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../resource.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")

namespace sso::gui {

using namespace Gdiplus;

namespace {

constexpr float kPi = 3.14159265358979f;

// --- Palette (dark) ---
const Color kBg(255, 20, 20, 25);
const Color kPanel(255, 30, 30, 38);
const Color kControl(255, 46, 46, 56);
const Color kControlHi(255, 58, 58, 70);
const Color kAccent(255, 0, 190, 230);
const Color kAccentDim(255, 0, 120, 150);
const Color kText(255, 226, 226, 232);
const Color kTextDim(255, 140, 140, 152);
const Color kWhite(255, 245, 245, 250);
const Color kGreen(255, 40, 200, 90);
const Color kYellow(255, 235, 190, 40);
const Color kRed(255, 235, 60, 60);

const int kWinW = 700;
const int kWinH = 480;
const UINT kTrayMsg = WM_APP + 1;

// --- Layout ---
const int kPowerCx = 30, kPowerCy = 32, kPowerR = 14;

const RECT kPresetRects[3] = {
    {  16, 64, 144,  96 },
    { 150, 64, 278,  96 },
    { 284, 64, 412,  96 },
};

const RECT kToggleRects[5] = {
    {  16, 112, 140, 146 },
    { 150, 112, 274, 146 },
    { 284, 112, 408, 146 },
    { 418, 112, 542, 146 },
    { 552, 112, 676, 146 },
};

const RECT kMeterRect = { 16, 372, 684, 430 };

// --- Parameter knobs ---
enum {
    K_HP = 0, K_LOW, K_MID, K_HIGH, K_LIFT, K_TRANS, K_SELF, K_GUNSHOT, K_CEILING
};

struct KnobSpec {
    int id;
    const wchar_t* label;
    int max;
};

const KnobSpec kKnobs[] = {
    { K_HP,      L"High-pass",   140 },
    { K_LOW,     L"Low-mid",     80  },
    { K_MID,     L"Mid",         80  },
    { K_HIGH,    L"High",        80  },
    { K_LIFT,    L"Distant lift",20  },
    { K_TRANS,   L"Transient",   10  },
    { K_SELF,    L"Self-step",   15  },
    { K_GUNSHOT, L"Gun reduce",  12  },
    { K_CEILING, L"Ceiling",     110 },
};

const int kKnobCount = 9;
const int kKnobR = 26;

const audio::Feature kFeatures[5] = {
    audio::Feature::Eq, audio::Feature::UpComp, audio::Feature::Transient,
    audio::Feature::SelfSuppress, audio::Feature::Limiter
};
const wchar_t* kFeatureLabels[5] = {
    L"EQ", L"Compressor", L"Transient", L"Self-step", L"Limiter"
};

const presets::Preset kPresets[3] = {
    presets::Preset::Footstep, presets::Preset::Balanced, presets::Preset::GunshotSafe
};
const wchar_t* kPresetLabels[3] = { L"Footstep", L"Balanced", L"Gunshot-safe" };

// --- Knob param mapping (int position -> Config field) ---
void ApplyKnobPos(Config& c, int id, int pos) {
    switch (id) {
        case K_HP:      c.hpFreqHz = 60.0f + pos; break;
        case K_LOW:     c.eqBands[0].gainDb = pos * 0.1f; break;
        case K_MID:     c.eqBands[1].gainDb = pos * 0.1f; break;
        case K_HIGH:    c.eqBands[2].gainDb = pos * 0.1f; break;
        case K_LIFT:    c.upCompMaxGainDb = static_cast<float>(pos); break;
        case K_TRANS:   c.transAmount = pos * 0.5f; break;
        case K_SELF:    c.selfSuppressDepthDb = static_cast<float>(pos); break;
        case K_GUNSHOT: c.gunshotReduceAmountDb = static_cast<float>(pos); break;
        case K_CEILING: c.limiterCeilingDb = -12.0f + pos * 0.1f; break;
        default: break;
    }
}

int KnobPosFromConfig(const Config& c, int id) {
    switch (id) {
        case K_HP:      return static_cast<int>(c.hpFreqHz - 60.0f + 0.5f);
        case K_LOW:     return static_cast<int>(c.eqBands[0].gainDb * 10.0f + 0.5f);
        case K_MID:     return static_cast<int>(c.eqBands[1].gainDb * 10.0f + 0.5f);
        case K_HIGH:    return static_cast<int>(c.eqBands[2].gainDb * 10.0f + 0.5f);
        case K_LIFT:    return static_cast<int>(c.upCompMaxGainDb + 0.5f);
        case K_TRANS:   return static_cast<int>(c.transAmount * 2.0f + 0.5f);
        case K_SELF:    return static_cast<int>(c.selfSuppressDepthDb + 0.5f);
        case K_GUNSHOT: return static_cast<int>(c.gunshotReduceAmountDb + 0.5f);
        case K_CEILING: return static_cast<int>((c.limiterCeilingDb + 12.0f) * 10.0f + 0.5f);
        default: return 0;
    }
}

void FormatKnobValue(int id, int pos, wchar_t* buf, size_t len) {
    switch (id) {
        case K_HP:      _snwprintf_s(buf, len, _TRUNCATE, L"%d Hz", 60 + pos); break;
        case K_LOW:
        case K_MID:
        case K_HIGH:    _snwprintf_s(buf, len, _TRUNCATE, L"%.1f dB", pos * 0.1f); break;
        case K_LIFT:    _snwprintf_s(buf, len, _TRUNCATE, L"%d dB", pos); break;
        case K_TRANS:   _snwprintf_s(buf, len, _TRUNCATE, L"%.1f dB", pos * 0.5f); break;
        case K_SELF:    _snwprintf_s(buf, len, _TRUNCATE, L"%d dB", pos); break;
        case K_GUNSHOT: _snwprintf_s(buf, len, _TRUNCATE, L"%d dB", pos); break;
        case K_CEILING: _snwprintf_s(buf, len, _TRUNCATE, L"%.1f dB", -12.0f + pos * 0.1f); break;
        default: break;
    }
}

int KnobMax(int id) {
    for (const auto& k : kKnobs)
        if (k.id == id) return k.max;
    return 1;
}

int KnobCx(int i) {
    const int cols = (i < 5) ? 5 : 4;
    const int col = (i < 5) ? i : (i - 5);
    const int spacing = 130;
    const int total = cols * spacing;
    const int startX = (kWinW - total) / 2 + spacing / 2;
    return startX + col * spacing;
}
int KnobCy(int i) { return (i < 5) ? 195 : 290; }

// --- Drawing helpers ---
void FillRoundRect(Graphics& g, const RECT& r, float radius, Color c) {
    GraphicsPath path;
    path.AddArc((REAL)r.left, (REAL)r.top, radius * 2, radius * 2, 180, 90);
    path.AddArc((REAL)r.right - radius * 2, (REAL)r.top, radius * 2, radius * 2, 270, 90);
    path.AddArc((REAL)r.right - radius * 2, (REAL)r.bottom - radius * 2, radius * 2, radius * 2, 0, 90);
    path.AddArc((REAL)r.left, (REAL)r.bottom - radius * 2, radius * 2, radius * 2, 90, 90);
    path.CloseFigure();
    SolidBrush b(c);
    g.FillPath(&b, &path);
}

void FillCircle(Graphics& g, float cx, float cy, float r, Color c) {
    SolidBrush b(c);
    g.FillEllipse(&b, cx - r, cy - r, r * 2, r * 2);
}

void DrawCenteredText(Graphics& g, const wchar_t* s, Font& f, Color c, const RECT& r) {
    SolidBrush b(c);
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    RectF rf((REAL)r.left, (REAL)r.top, (REAL)(r.right - r.left), (REAL)(r.bottom - r.top));
    g.DrawString(s, -1, &f, rf, &fmt, &b);
}

void DrawKnob(Graphics& g, int cx, int cy, int r, float v) {
    FillCircle(g, (float)cx, (float)cy, (float)r, kControl);

    Pen ring(kControlHi, 2.0f);
    g.DrawEllipse(&ring, cx - r, cy - r, r * 2, r * 2);

    if (v > 0.001f) {
        Pen arc(kAccent, 4.0f);
        arc.SetStartCap(LineCapRound);
        arc.SetEndCap(LineCapRound);
        g.DrawArc(&arc, RectF((REAL)(cx - r + 5), (REAL)(cy - r + 5),
                              (REAL)((r - 5) * 2), (REAL)((r - 5) * 2)),
                  135.0f, 270.0f * v);
    }

    const float a = (135.0f + 270.0f * v) * kPi / 180.0f;
    const float px = cx + std::cos(a) * (r - 8);
    const float py = cy + std::sin(a) * (r - 8);
    Pen ptr(kWhite, 3.0f);
    ptr.SetStartCap(LineCapRound);
    ptr.SetEndCap(LineCapRound);
    g.DrawLine(&ptr, (REAL)cx, (REAL)cy, px, py);

    FillCircle(g, (float)cx, (float)cy, 3.0f, kWhite);
}

void DrawToggle(Graphics& g, const RECT& r, bool on, const wchar_t* label) {
    Font labelFont(L"Segoe UI", 10);
    DrawCenteredText(g, label, labelFont, on ? kText : kTextDim,
                     RECT{ r.left, r.top, r.right, r.top + 18 });

    const int pillW = 44, pillH = 20;
    const int px = (r.left + r.right) / 2 - pillW / 2;
    const int py = r.top + 22;
    RECT pill{ px, py, px + pillW, py + pillH };
    FillRoundRect(g, pill, pillH / 2.0f, on ? kAccent : kControl);

    const int knobR = pillH / 2 - 3;
    const int kx = on ? (px + pillW - knobR - 3) : (px + knobR + 3);
    FillCircle(g, (float)kx, (float)(py + pillH / 2), (float)knobR, kWhite);
}

void DrawPower(Graphics& g, int cx, int cy, int r, bool on) {
    FillCircle(g, (float)cx, (float)cy, (float)r, on ? kAccent : kControl);
    Pen ring(kControlHi, 2.0f);
    g.DrawEllipse(&ring, cx - r, cy - r, r * 2, r * 2);

    // Power glyph: a vertical bar + an arc.
    Pen glyph(on ? kWhite : kTextDim, 2.0f);
    glyph.SetStartCap(LineCapRound);
    glyph.SetEndCap(LineCapRound);
    g.DrawLine(&glyph, (REAL)cx, (REAL)(cy - r * 0.45f), (REAL)cx, (REAL)(cy + r * 0.15f));
    g.DrawArc(&glyph, RectF((REAL)(cx - r * 0.45f), (REAL)(cy - r * 0.35f),
                            (REAL)(r * 0.9f), (REAL)(r * 0.9f)), -60.0f, 300.0f);
}

void DrawMeter(Graphics& g, const RECT& r, float db) {
    FillRoundRect(g, r, 5.0f, kPanel);

    Font labelFont(L"Segoe UI", 10);
    Font tickFont(L"Segoe UI", 8);
    SolidBrush tickBrush(kTextDim);
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);

    // Scale: -60 .. 0 dB mapped across the width.
    const int w = r.right - r.left;
    auto X = [&](float d) -> int { return r.left + (int)(w * (d + 60.0f) / 60.0f); };

    const float cur = std::min(0.0f, std::max(-60.0f, db));
    const int curX = X(cur);

    auto fill = [&](float a, float b, Color c) {
        const int xa = X(a), xb = std::min(curX, X(b));
        if (xb <= xa) return;
        SolidBrush br(c);
        g.FillRectangle(&br, xa, r.top + 2, xb - xa, r.bottom - r.top - 4);
    };
    fill(-60.0f, -20.0f, kGreen);
    fill(-20.0f, -6.0f, kYellow);
    fill(-6.0f, 0.0f, kRed);

    // Tick marks + labels.
    const float ticks[] = { -60.0f, -40.0f, -20.0f, -6.0f, 0.0f };
    for (float t : ticks) {
        const int x = X(t);
        Pen p(kControlHi, 1.0f);
        g.DrawLine(&p, x, r.bottom, x, r.bottom - 6);
        wchar_t buf[16];
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.0f", t);
        RectF rf((REAL)(x - 20), (REAL)(r.bottom - 2), 40.0f, 16.0f);
        g.DrawString(buf, -1, &tickFont, rf, &fmt, &tickBrush);
    }

}

} // namespace

// Peak-hold line needs the controller's displayed meter value; drawn in Draw().
static void DrawPeakLine(Graphics& g, const RECT& r, int peakX) {
    Pen p(kWhite, 1.0f);
    g.DrawLine(&p, peakX, r.top, peakX, r.bottom);
}

bool GuiController::Init(HINSTANCE instance, AppContext* ctx) {
    inst_ = instance;
    ctx_ = ctx;
    config_ = presets::make(presets::Preset::Balanced);

    GdiplusStartupInput gsi;
    if (GdiplusStartup(&gdiplusToken_, &gsi, nullptr) != Ok) return false;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &GuiController::WndProc;
    wc.hInstance = inst_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst_, MAKEINTRESOURCEW(IDI_MAIN));
    wc.lpszClassName = L"SoundStepWindow";
    if (!RegisterClassExW(&wc)) return false;

    RECT rc{ 0, 0, kWinW, kWinH };
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"SoundStep",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                            nullptr, nullptr, inst_, this);
    if (!hwnd_) return false;

    // Dark title bar (Windows 10 1809+ / 11).
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    return true;
}

void GuiController::Run() {
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void GuiController::Shutdown() {
    RemoveTrayIcon();
    if (gdiplusToken_) {
        GdiplusShutdown(gdiplusToken_);
        gdiplusToken_ = 0;
    }
}

void GuiController::ApplyPreset(presets::Preset p) {
    preset_ = p;
    config_ = presets::make(p);
    PublishConfig();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void GuiController::PublishConfig() {
    ctx_->pipeline->SetConfig(config_);
}

void GuiController::ToggleBypass() {
    ctx_->pipeline->SetBypass(!ctx_->pipeline->bypass());
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void GuiController::OnMouseDown(int x, int y) {
    // Power toggle?
    if ((x - kPowerCx) * (x - kPowerCx) + (y - kPowerCy) * (y - kPowerCy) <=
        (kPowerR + 4) * (kPowerR + 4)) {
        ToggleBypass();
        return;
    }

    // Presets.
    for (int i = 0; i < 3; ++i) {
        if (PtInRect(&kPresetRects[i], POINT{ x, y })) {
            ApplyPreset(kPresets[i]);
            return;
        }
    }

    // Feature toggles.
    for (int i = 0; i < 5; ++i) {
        if (PtInRect(&kToggleRects[i], POINT{ x, y })) {
            audio::Feature f = kFeatures[i];
            ctx_->pipeline->SetFeatureEnabled(f, !ctx_->pipeline->featureEnabled(f));
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }

    // Knobs.
    for (int i = 0; i < kKnobCount; ++i) {
        const int cx = KnobCx(i);
        const int dx = x - cx, dy = y - KnobCy(i);
        if (dx * dx + dy * dy <= (kKnobR + 6) * (kKnobR + 6)) {
            dragKnob_ = kKnobs[i].id;
            dragStartPos_ = KnobPosFromConfig(config_, dragKnob_);
            dragStartY_ = y;
            SetCapture(hwnd_);
            return;
        }
    }
}

void GuiController::OnMouseMove(int, int y) {
    if (dragKnob_ < 0) return;
    const int max = KnobMax(dragKnob_);
    // Drag up = increase; ~2 px per step.
    const int delta = (dragStartY_ - y) / 2;
    int pos = dragStartPos_ + delta;
    if (pos < 0) pos = 0;
    if (pos > max) pos = max;
    ApplyKnobPos(config_, dragKnob_, pos);
    PublishConfig();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void GuiController::OnMouseUp() {
    if (dragKnob_ >= 0) {
        ReleaseCapture();
        dragKnob_ = -1;
    }
}

void GuiController::Draw(Gdiplus::Graphics& g, int w, int) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.Clear(kBg);

    const auto* pipe = ctx_->pipeline;

    // Header.
    Font titleFont(L"Segoe UI", 18, FontStyleBold);
    Font subFont(L"Segoe UI", 10);
    SolidBrush titleBrush(kText);
    SolidBrush subBrush(kTextDim);
    g.DrawString(L"SoundStep", -1, &titleFont, PointF(54, 12), &titleBrush);
    g.DrawString(L"PUBG audio processor", -1, &subFont, PointF(56, 40), &subBrush);

    DrawPower(g, kPowerCx, kPowerCy, kPowerR, !pipe->bypass());

    // Status (right side of header).
    wchar_t status[128];
    const DWORD pid = ctx_->activePid.load(std::memory_order_relaxed);
    if (pid != 0) {
        _snwprintf_s(status, _countof(status), _TRUNCATE, L"Processing PID %lu",
                     static_cast<unsigned long>(pid));
    } else {
        _snwprintf_s(status, _countof(status), _TRUNCATE, L"Waiting for TslGame.exe...");
    }
    Font statusFont(L"Segoe UI", 10);
    SolidBrush statusBrush(pid ? kGreen : kTextDim);
    StringFormat rightFmt;
    rightFmt.SetAlignment(StringAlignmentFar);
    RectF statusRf((REAL)120, 16.0f, (REAL)(w - 132), 24.0f);
    g.DrawString(status, -1, &statusFont, statusRf, &rightFmt, &statusBrush);

    // Presets.
    Font presetFont(L"Segoe UI", 11, FontStyleBold);
    for (int i = 0; i < 3; ++i) {
        const bool active = (kPresets[i] == preset_);
        FillRoundRect(g, kPresetRects[i], 8.0f, active ? kAccent : kPanel);
        DrawCenteredText(g, kPresetLabels[i], presetFont,
                         active ? Color(255, 10, 20, 24) : kText, kPresetRects[i]);
    }

    // Feature toggles.
    for (int i = 0; i < 5; ++i) {
        DrawToggle(g, kToggleRects[i], pipe->featureEnabled(kFeatures[i]), kFeatureLabels[i]);
    }

    // Knobs.
    Font knobLabel(L"Segoe UI", 10);
    Font knobValue(L"Segoe UI", 10, FontStyleBold);
    for (int i = 0; i < kKnobCount; ++i) {
        const int id = kKnobs[i].id;
        const int max = kKnobs[i].max;
        const int pos = KnobPosFromConfig(config_, id);
        const float v = (float)pos / (float)max;
        const int cx = KnobCx(i);
        const int cy = KnobCy(i);
        DrawKnob(g, cx, cy, kKnobR, v);

        wchar_t val[32];
        FormatKnobValue(id, pos, val, _countof(val));
        RECT lr{ cx - 60, cy + kKnobR + 6, cx + 60, cy + kKnobR + 24 };
        RECT vr{ cx - 60, cy + kKnobR + 24, cx + 60, cy + kKnobR + 42 };
        DrawCenteredText(g, kKnobs[i].label, knobLabel, kTextDim, lr);
        DrawCenteredText(g, val, knobValue, kText, vr);
    }

    // Meter.
    DrawMeter(g, kMeterRect, meterDb_);
    {
        const int meterW = kMeterRect.right - kMeterRect.left;
        const float cur = std::min(0.0f, std::max(-60.0f, meterDb_));
        const int peakX = kMeterRect.left + (int)(meterW * (cur + 60.0f) / 60.0f);
        DrawPeakLine(g, kMeterRect, peakX);
    }
    Font meterLabel(L"Segoe UI", 10);
    SolidBrush meterBrush(kTextDim);
    g.DrawString(L"OUTPUT", -1, &meterLabel, PointF(16, 356), &meterBrush);
}

void GuiController::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int w = rc.right, h = rc.bottom;

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);

    Graphics g(mem);
    Draw(g, w, h);

    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd_, &ps);
}

void GuiController::AddTrayIcon() {
    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = kTrayMsg;
    nid_.hIcon = LoadIconW(inst_, MAKEINTRESOURCEW(IDI_MAIN));
    wcscpy_s(nid_.szTip, L"SoundStep");
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &nid_) == TRUE;
}

void GuiController::RemoveTrayIcon() {
    if (trayAdded_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        trayAdded_ = false;
    }
}

void GuiController::ShowWindowFromTray() {
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void GuiController::ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Show SoundStep");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (ctx_->pipeline->bypass() ? 0 : MF_CHECKED), 2, L"Enable processing");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"Preset: Footstep");
    AppendMenuW(menu, MF_STRING, 4, L"Preset: Balanced");
    AppendMenuW(menu, MF_STRING, 5, L"Preset: Gunshot-safe");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 6, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                    pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
        case 1: ShowWindowFromTray(); break;
        case 2: ToggleBypass(); break;
        case 3: ApplyPreset(presets::Preset::Footstep); break;
        case 4: ApplyPreset(presets::Preset::Balanced); break;
        case 5: ApplyPreset(presets::Preset::GunshotSafe); break;
        case 6: DestroyWindow(hwnd_); break;
        default: break;
    }
    PostMessageW(hwnd_, WM_NULL, 0, 0);
}

LRESULT CALLBACK GuiController::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    GuiController* self = nullptr;
    if (msg == WM_NCCREATE) {
        const CREATESTRUCT* cs = reinterpret_cast<const CREATESTRUCT*>(lParam);
        self = static_cast<GuiController*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<GuiController*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT GuiController::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            AddTrayIcon();
            RegisterHotKey(hwnd, 1, 0, VK_F8);
            SetTimer(hwnd, 1, 40, nullptr);
            return 0;

        case WM_PAINT:
            OnPaint();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_LBUTTONDOWN:
            OnMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_MOUSEMOVE:
            OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_LBUTTONUP:
            OnMouseUp();
            return 0;

        case WM_CAPTURECHANGED:
            OnMouseUp();
            return 0;

        case WM_HOTKEY:
            if (wParam == 1) ToggleBypass();
            return 0;

        case WM_TIMER: {
            // Peak-hold + decay for the meter.
            const float cur = ctx_->pipeline->outputPeakDb();
            if (cur > meterDb_) meterDb_ = cur;
            else meterDb_ = std::max(-60.0f, meterDb_ - 0.5f);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case kTrayMsg:
            switch (lParam) {
                case WM_RBUTTONUP:
                case WM_CONTEXTMENU:
                    ShowTrayMenu();
                    break;
                case WM_LBUTTONDBLCLK:
                    ShowWindowFromTray();
                    break;
                default:
                    break;
            }
            return 0;

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);   // minimize to tray
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, 1);
            UnregisterHotKey(hwnd, 1);
            RemoveTrayIcon();
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace sso::gui
