#include "app_state.h"

std::vector<Screen> Screens() {
    std::vector<Screen> result;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info))
                reinterpret_cast<std::vector<Screen>*>(data)->push_back(
                    {info.szDevice, info.rcMonitor, (info.dwFlags & MONITORINFOF_PRIMARY) != 0});
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&result));
    std::sort(result.begin(), result.end(),
              [](const Screen& a, const Screen& b) { return a.device < b.device; });
    return result;
}
BOOL CALLBACK FindWorker(HWND top, LPARAM output) {
    if (FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr)) {
        HWND worker = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
        if (worker) {
            *reinterpret_cast<HWND*>(output) = worker;
            return FALSE;
        }
    }
    return TRUE;
}
RECT DesktopRect() {
    if (!state->monitorDevice.empty()) {
        for (const auto& screen : Screens())
            if (screen.device == state->monitorDevice) {
                state->active = true;
                return screen.rect;
            }
        state->active = false;
        return {0, 0, 1, 1};
    }
    if (state->options.span) {
        LONG x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        return {x, y, x + GetSystemMetrics(SM_CXVIRTUALSCREEN), y + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    }
    POINT origin{};
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &info);
    return info.rcMonitor;
}
void AttachDesktop() {
    HWND progman = FindWindowW(L"Progman", nullptr), host = nullptr;
    if (!progman)
        throw Failure{HRESULT_FROM_WIN32(ERROR_NOT_FOUND), L"Explorer / Progman absent"};
    DWORD_PTR result = 0;
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_ABORTIFHUNG, 1000, &result);
    HWND icons = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
    HWND background = FindWindowExW(progman, nullptr, L"WorkerW", nullptr);
    bool raised =
        icons && background && (GetWindowLongPtrW(progman, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP);
    if (raised)
        host = progman;
    else
        EnumWindows(FindWorker, reinterpret_cast<LPARAM>(&host));
    if (!host)
        host = progman;
    SetLastError(0);
    HWND previous = SetParent(state->window, host);
    if (!previous && GetLastError())
        throw Failure{HRESULT_FROM_WIN32(GetLastError()), L"SetParent WorkerW"};
    state->host = host;
    RECT r = DesktopRect();
    POINT p{r.left, r.top};
    MapWindowPoints(HWND_DESKTOP, host, &p, 1);
    Check(SetWindowPos(state->window, raised ? icons : HWND_BOTTOM, p.x, p.y, r.right - r.left,
                       r.bottom - r.top,
                       SWP_NOACTIVATE |
                           (state->active && (!V0Build || !state->options.video.empty()) ? SWP_SHOWWINDOW
                                                                                         : SWP_HIDEWINDOW))
              ? S_OK
              : HRESULT_FROM_WIN32(GetLastError()),
          L"Position du fond vidéo");
    wchar_t diagnostic[240]{};
    swprintf_s(diagnostic, L"Desktop attach: raised=%u hwnd=%p host=%p icons=%p background=%p size=%ldx%ld",
               raised, state->window, host, icons, background, r.right - r.left, r.bottom - r.top);
    Log(diagnostic);
}
bool Fullscreen() {
    HWND foreground = GetForegroundWindow();
    if (!foreground || foreground == state->window || foreground == state->host ||
        foreground == FindWindowW(L"Progman", nullptr))
        return false;
    wchar_t name[64]{};
    GetClassNameW(foreground, name, 64);
    if (wcscmp(name, L"WorkerW") == 0 || wcscmp(name, L"Shell_TrayWnd") == 0)
        return false;
    DWORD cloaked = 0;
    DwmGetWindowAttribute(foreground, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked || IsIconic(foreground))
        return false;
    RECT r{};
    if (FAILED(DwmGetWindowAttribute(foreground, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))) &&
        !GetWindowRect(foreground, &r))
        return false;
    MONITORINFO m{sizeof(m)};
    if (!GetMonitorInfoW(MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST), &m))
        return false;
    if (state->monitorDevice.empty() && !state->options.span && !(m.dwFlags & MONITORINFOF_PRIMARY))
        return false;
    if (!state->monitorDevice.empty()) {
        MONITORINFOEXW monitor{};
        monitor.cbSize = sizeof(monitor);
        if (!GetMonitorInfoW(MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST), &monitor) ||
            state->monitorDevice != monitor.szDevice)
            return false;
    }
    return r.left <= m.rcMonitor.left + 1 && r.top <= m.rcMonitor.top + 1 &&
           r.right >= m.rcMonitor.right - 1 && r.bottom >= m.rcMonitor.bottom - 1;
}
void UpdatePause() {
    SYSTEM_POWER_STATUS power{};
    bool onBattery = state->options.battery && GetSystemPowerStatus(&power) && power.ACLineStatus == 0;
    bool paused = !state->active || state->root->locked || state->root->sleeping ||
                  state->root->manualPause || onBattery || Fullscreen();
    if (state->pause.exchange(paused) != paused)
        SetEvent(state->wake.value);
}
void CreateTargetWindow(State& target) {
    State* previous = state;
    state = &target;
    if (!target.wake.value)
        target.wake.value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!target.wake.value) {
        state = previous;
        throw Failure{HRESULT_FROM_WIN32(GetLastError()), L"Événement écran"};
    }
    target.window =
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, ClassName, L"PlumeWallpaper", WS_POPUP, 0, 0, 1,
                        1, nullptr, nullptr, GetModuleHandleW(nullptr), &target);
    if (!target.window) {
        state = previous;
        throw Failure{HRESULT_FROM_WIN32(GetLastError()), L"Fenêtre écran"};
    }
    SetWindowLongPtrW(target.window, GWL_STYLE, WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
    try {
        AttachDesktop();
        UpdatePause();
    } catch (...) {
        state = previous;
        throw;
    }
    state = previous;
}
