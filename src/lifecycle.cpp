#include "app_state.h"
#include "ui/settings.h"

void StopAll() {
    application->stop = true;
    SetEvent(application->wake.value);
    for (auto& child : application->children) {
        child->stop = true;
        SetEvent(child->wake.value);
    }
}
void Tray(bool remove) {
    NOTIFYICONDATAW icon{sizeof(icon)};
    icon.hWnd = state->window;
    icon.uID = 1;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon.uCallbackMessage = TrayMessage;
    icon.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_PLUME));
    wcscpy_s(icon.szTip, L"Plume — fond vidéo (clic droit pour quitter)");
    Shell_NotifyIconW(remove ? NIM_DELETE : NIM_ADD, &icon);
}
LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_NCCREATE)
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
    State* target = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    struct Scope {
        State* previous;
        ~Scope() {
            state = previous;
        }
    } scope{state};
    if (target)
        state = target;
    if (!state)
        return DefWindowProcW(hwnd, message, w, l);
    if (message == state->taskbarMessage && state->taskbarMessage != 0) {
        Tray();
        try {
            AttachDesktop();
        } catch (...) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;
    }
    switch (message) {
    case WM_SIZE:
        state->size = (UINT64(HIWORD(l)) << 32) | LOWORD(l);
        SetEvent(state->wake.value);
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        try {
            ForTargets(AttachDesktop);
            ForTargets(UpdatePause);
        } catch (...) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        if constexpr (V0Build)
            RefreshSettings(true);
        return 0;
    case WM_TIMER:
        Tick();
        return 0;
    case WM_WTSSESSION_CHANGE:
        if (w == WTS_SESSION_LOCK || w == WTS_CONSOLE_DISCONNECT || w == WTS_REMOTE_DISCONNECT)
            application->locked = true;
        if (w == WTS_SESSION_UNLOCK || w == WTS_CONSOLE_CONNECT || w == WTS_REMOTE_CONNECT)
            application->locked = false;
        ForTargets(UpdatePause);
        return 0;
    case WM_POWERBROADCAST:
        if (w == PBT_APMSUSPEND)
            application->sleeping = true;
        if (w == PBT_APMRESUMEAUTOMATIC || w == PBT_APMRESUMESUSPEND)
            application->sleeping = false;
        ForTargets(UpdatePause);
        return TRUE;
    case TrayMessage:
        if constexpr (V0Build)
            if (l == WM_LBUTTONUP || l == WM_LBUTTONDBLCLK) {
                OpenSettings();
                return 0;
            }
        if (l == WM_RBUTTONUP || l == WM_CONTEXTMENU) {
            HMENU menu = CreatePopupMenu();
            if constexpr (V0Build)
                AppendMenuW(menu, MF_STRING, 3, L"Vidéos et écrans…");
            AppendMenuW(menu, MF_STRING | (application->manualPause ? MF_CHECKED : 0), 1, L"Pause manuelle");
            auto screens = Screens();
            for (size_t i = 0; i < screens.size(); ++i) {
                std::wstring label = L"Choisir une vidéo pour " + screens[i].device +
                                     (screens[i].primary ? L" (principal)" : L"");
                AppendMenuW(menu, MF_STRING, 100 + i, label.c_str());
            }
            AppendMenuW(menu, MF_STRING, 2, L"Quitter");
            POINT p{};
            GetCursorPos(&p);
            // Child desktop windows cannot reliably own a foreground popup.
            HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP, 0, 0, 0, 0, nullptr,
                                         nullptr, nullptr, nullptr);
            ShowWindow(owner, SW_SHOWNA);
            SetForegroundWindow(owner);
            UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, p.x, p.y, 0,
                                          owner, nullptr);
            DestroyWindow(owner);
            DestroyMenu(menu);
            if (command == 1) {
                application->manualPause = !application->manualPause;
                ForTargets(UpdatePause);
            }
            if constexpr (V0Build)
                if (command == 3)
                    OpenSettings();
            if (command >= 100 && command < 100 + screens.size()) {
                try {
                    ChooseScreen(screens[command - 100]);
                } catch (Failure f) {
                    wchar_t text[512]{};
                    swprintf_s(text, L"%s (0x%08X)", f.operation, unsigned(f.hr));
                    MessageBoxW(nullptr, text, L"Plume", MB_OK | MB_ICONERROR);
                }
            }
            if (command == 2)
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;
    case ErrorMessage:
        if constexpr (V0Build)
            if (!application->testMode) {
                HandleV0Error(*state);
                return 0;
            }
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
    case WM_CLOSE:
        StopAll();
        PostQuitMessage(0);
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (w) {
            StopAll();
            PostQuitMessage(0);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, message, w, l);
}

void StartTarget(State& target) {
    target.stop = false;
    target.error = {S_OK, L""};
    if constexpr (V0Build)
        if (target.options.video.empty())
            return;
    target.playback = std::thread([&target] {
        state = &target;
        Playback();
    });
}
void Tick() {
    State* previous = state;
    state = application;
    struct Restore {
        State* p;
        ~Restore() {
            state = p;
        }
    } restore{previous};
    if (application->deadline && GetTickCount64() >= application->deadline) {
        StopAll();
        PostQuitMessage(0);
        return;
    }
    if constexpr (V0Build)
        TickSettings();
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                             sizeof(memory))) {
        application->peakWorkingSet = std::max(application->peakWorkingSet, memory.WorkingSetSize);
        application->peakPrivateBytes = std::max(application->peakPrivateBytes, memory.PrivateUsage);
        if constexpr (V0Build)
            if (GetTickCount64() >= application->nextDiagnostics) {
                wchar_t diagnostic[240]{};
                swprintf_s(diagnostic, L"Session RAM: WS=%.2f MiB privateCommit=%.2f MiB",
                           memory.WorkingSetSize / 1048576.0, memory.PrivateUsage / 1048576.0);
                Log(diagnostic);
                ForTargets([] {
                    wchar_t text[240]{};
                    swprintf_s(text,
                               L"Session écran: frames=%llu previews=%llu pause=%u active=%u visible=%u",
                               state->frames.load(), state->previewFrames.load(), state->pause.load(),
                               state->active, IsWindowVisible(state->window));
                    Log(text);
                });
                application->nextDiagnostics = GetTickCount64() + 10000;
            }
    }
    if (application->auditMemory && GetTickCount64() >= application->nextAudit) {
        Audit(L"periodic");
        application->nextAudit = GetTickCount64() + 5000;
    }
    try {
        auto repair = [](State& target) {
            State* prior = state;
            state = &target;
            if (!IsWindow(target.window) || target.stop) {
                target.stop = true;
                SetEvent(target.wake.value);
                if (target.playback.joinable())
                    target.playback.join();
                if (IsWindow(target.window))
                    DestroyWindow(target.window);
                CreateTargetWindow(target);
                StartTarget(target);
                if (&target == application) {
                    WTSRegisterSessionNotification(target.window, NOTIFY_FOR_THIS_SESSION);
                    Tray();
                }
                Log(L"Fenêtre recréée après perte Explorer");
            } else if (!IsWindow(target.host))
                AttachDesktop();
            else {
                HWND progman = FindWindowW(L"Progman", nullptr);
                HWND icons = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
                HWND background = FindWindowExW(progman, nullptr, L"WorkerW", nullptr);
                if (target.host == progman && icons && background) {
                    bool foundIcons = false;
                    for (HWND sibling = GetWindow(target.window, GW_HWNDPREV); sibling;
                         sibling = GetWindow(sibling, GW_HWNDPREV)) {
                        if (sibling == background)
                            break;
                        if (sibling == icons) {
                            foundIcons = true;
                            break;
                        }
                    }
                    if (!foundIcons)
                        AttachDesktop();
                }
            }
            state = prior;
        };
        repair(*application);
        for (auto& target : application->children)
            repair(*target);
        ForTargets(UpdatePause);
    } catch (...) {
        state = application; /* Explorer may still be restarting: retry next timer tick. */
    }
}
