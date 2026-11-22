#include <windows.h>
#include <objbase.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include "audio/audio_pipeline.h"
#include "audio/process_monitor.h"
#include "config.h"
#include "gui/gui_controller.h"

using namespace sso;

namespace {

// Reports the OS build number so we can verify process-loopback support
// (requires Windows 10 build 20348 / Windows 11).
void PrintOsVersion() {
    using RtlGetVersion_t = LONG(WINAPI*)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto RtlGetVersion = reinterpret_cast<RtlGetVersion_t>(
            GetProcAddress(ntdll, "RtlGetVersion"));
        if (RtlGetVersion) {
            OSVERSIONINFOW vi{};
            vi.dwOSVersionInfoSize = sizeof(vi);
            if (RtlGetVersion(&vi) == 0) {
                std::printf("OS: %lu.%lu.%lu.%lu\n",
                            vi.dwMajorVersion, vi.dwMinorVersion,
                            vi.dwBuildNumber, vi.dwPlatformId);
            }
        }
    }
}

} // namespace

int wmain() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::printf("CoInitializeEx failed: 0x%08X\n", static_cast<unsigned>(hr));
        return 1;
    }

    PrintOsVersion();

    audio::AudioPipeline pipeline;    gui::AppContext ctx;
    ctx.pipeline = &pipeline;

    gui::GuiController gui;
    if (!gui.Init(GetModuleHandleW(nullptr), &ctx)) {
        std::printf("Failed to create window.\n");
        CoUninitialize();
        return 1;
    }

    std::atomic<bool> quit{false};
    std::thread watcher([&]() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        while (!quit.load(std::memory_order_relaxed)) {
            const DWORD pid = FindProcessIdByName(kTargetProcessName);
            if (pid != 0 && !pipeline.running()) {
                std::printf("Detected %ls (PID %lu), engaging processing...\n",
                            kTargetProcessName, static_cast<unsigned long>(pid));
                pipeline.Start(pid);
            } else if (pid == 0 && pipeline.running()) {
                std::printf("%ls closed, releasing audio routing...\n", kTargetProcessName);
                pipeline.Stop();
            }
            ctx.activePid.store(pipeline.running() ? pid : 0, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        CoUninitialize();
    });

    gui.Run();

    quit.store(true);
    watcher.join();
    pipeline.Stop();
    gui.Shutdown();
    CoUninitialize();
    return 0;
}
