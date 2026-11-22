#pragma once
#include <windows.h>
#include <shellapi.h>

#include <atomic>

#include "../audio/audio_pipeline.h"
#include "../config.h"
#include "presets.h"

namespace Gdiplus { class Graphics; }

namespace sso::gui {

// Shared state between the watcher thread and the UI thread.
struct AppContext {
    audio::AudioPipeline* pipeline = nullptr;
    std::atomic<DWORD> activePid{0};   // PID currently being processed (0 = none)
};

// Custom-drawn dark UI (GDI+): rotary knobs, toggle switches, preset buttons,
// a live output level meter, and a system-tray icon.
class GuiController {
public:
    bool Init(HINSTANCE instance, AppContext* ctx);
    void Run();
    void Shutdown();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void Draw(Gdiplus::Graphics& g, int w, int h);
    void OnPaint();
    void OnMouseDown(int x, int y);
    void OnMouseMove(int x, int y);
    void OnMouseUp();

    void ApplyPreset(presets::Preset p);
    void PublishConfig();
    void ToggleBypass();
    void ShowTrayMenu();
    void AddTrayIcon();
    void RemoveTrayIcon();
    void ShowWindowFromTray();

    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    AppContext* ctx_ = nullptr;
    presets::Preset preset_ = presets::Preset::Balanced;
    Config config_{};

    ULONG_PTR gdiplusToken_ = 0;
    int dragKnob_ = -1;      // knob id being dragged, or -1
    int dragStartPos_ = 0;
    int dragStartY_ = 0;
    float meterDb_ = -60.0f; // peak-held output level (dB)

    NOTIFYICONDATAW nid_{};
    bool trayAdded_ = false;
};

} // namespace sso::gui
