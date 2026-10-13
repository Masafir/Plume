#pragma once
#include "platform.h"
#include "resources/resource.h"
#ifdef PLUME_V0
constexpr bool V0Build = true;
constexpr wchar_t ClassName[] = L"PlumeWallpaper.V0.Window";
#else
constexpr bool V0Build = false;
constexpr wchar_t ClassName[] = L"PlumeWallpaper.Window";
#endif
constexpr UINT TrayMessage = WM_APP + 1, ErrorMessage = WM_APP + 2;
constexpr DWORD VideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
constexpr DWORD AllStreams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
struct Options {
    std::wstring video, ini, source;
    bool fill = true, span = false, battery = true, compat = false;
    UINT importFps = 30;
};
struct SharedGpu {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    std::recursive_mutex renderMutex;
    SharedGpu();
};
struct Screen {
    std::wstring device;
    RECT rect{};
    bool primary = false;
};
struct PreviewSurface {
    HWND window = nullptr;
    UINT width = 0, height = 0;
    ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID3D11VideoProcessorOutputView> output;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
    bool failed = false;
    UINT64 nextFrame = 0;
};
struct State {
    HWND window = nullptr, host = nullptr;
    Options options;
    std::atomic<bool> stop{false}, pause{false};
    std::atomic<UINT64> size{0};
    Handle wake;
    std::thread playback;
    bool locked = false, sleeping = false, manualPause = false;
    UINT taskbarMessage = 0;
    Failure error{S_OK, L""}; // Published by PostMessage, read after joining thread.
    State* root = this;
    std::wstring monitorDevice;
    bool active = true;
    std::vector<std::unique_ptr<State>> children;
    std::shared_ptr<SharedGpu> gpu;
    std::mutex gpuMutex;
    std::mutex logMutex;
    FILE* report = nullptr;
    std::atomic<UINT64> frames{0}, samples{0};
    std::atomic<UINT64> previewFrames{0};
    std::mutex previewMutex;
    PreviewSurface preview;
    UINT64 deadline = 0;
    SIZE_T peakWorkingSet = 0, peakPrivateBytes = 0;
    bool testMode = false;
    UINT64 startedTick = GetTickCount64();
    UINT_PTR timer = 0;
    bool auditMemory = false;
    bool auditNoRender = false;
    UINT decoderSamples = 0;
    bool flushLoop = false;
    bool bounded = V0Build;
    bool boundedLowLatency = V0Build;
    bool uiSmokeTest = false;
    bool showSettings = false;
    std::wstring importTest;
    std::wstring importScreen;
    Handle openSettingsRequest;
    std::wstring previousVideo, previousSource;
    UINT64 nextAudit = 0;
    UINT64 nextDiagnostics = 0;
};

// One State per monitor. The root additionally owns application configuration and shared GPU.
// Only the UI thread edits options/children; stop and join playback before changing a target.
extern thread_local State* state;
extern State* application;
std::vector<Screen> Screens();
void Log(const wchar_t* text);
void Audit(const wchar_t* stage, bool detail = false);
void Playback();
void StartTarget(State& target);
void CreateTargetWindow(State& target);
void AttachDesktop();
void UpdatePause();
void StopAll();
void Tick();
void Tray(bool remove = false);
LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
std::wstring ConfigPath();
void Save(const Options&);
void SaveScreens();
void ChooseScreen(const Screen&);
void AutoStart(bool enable);
int RunApplication(HINSTANCE instance);

template <class F> void ForTargets(F fn) {
    State* previous = state;
    state = application;
    fn();
    for (auto& child : application->children) {
        state = child.get();
        fn();
    }
    state = previous;
}
