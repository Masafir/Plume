#include "settings_state.h"
#include "../video/bounded_mp4.h"
#include "version.h"

namespace v0 {
HWND panel = nullptr, screens = nullptr, path = nullptr, status = nullptr, memory = nullptr;
HWND progressBar = nullptr;
HFONT font = nullptr;
UINT dpi = 96;
std::vector<Screen> displays;
std::wstring message =
    L"Choisissez un écran, puis sa vidéo. Fermer cette fenêtre laisse le fond animé actif.";
State* Target(const std::wstring& device) {
    if (application->monitorDevice == device)
        return application;
    for (auto& child : application->children)
        if (child->monitorDevice == device)
            return child.get();
    return nullptr;
}
void Stop(State& t) {
    t.stop = true;
    SetEvent(t.wake.value);
    if (t.playback.joinable())
        t.playback.join();
    t.stop = false;
}
void Assign(const std::wstring& device, const std::wstring& video, const std::wstring& source) {
    BoundedMp4 validate(video, 4);
    State* t = Target(device);
    if (!t) {
        auto child = std::make_unique<State>();
        child->root = application;
        child->options = application->options;
        child->options.video.clear();
        child->options.source.clear();
        child->monitorDevice = device;
        t = child.get();
        application->children.push_back(std::move(child));
        CreateTargetWindow(*t);
    }
    Stop(*t);
    t->previousVideo = t->options.video;
    t->previousSource = t->options.source;
    t->options.video = video;
    t->options.source = source;
    State* prior = state;
    state = t;
    AttachDesktop();
    state = prior;
    StartTarget(*t);
    SaveScreens();
    RefreshSettings();
}
void Error(Failure f) {
    wchar_t text[600]{};
    swprintf_s(text, L"%s\nErreur 0x%08X", f.operation, static_cast<unsigned>(f.hr));
    if (!application->testMode)
        MessageBoxW(panel, text, L"Plume V0", MB_OK | MB_ICONERROR);
    message = text;
}
LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintPanel(hwnd);
        return 0;
    case WM_DRAWITEM:
        DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(l));
        return TRUE;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(w);
        SetTextColor(dc, Ink);
        SetBkColor(dc, Background);
        return reinterpret_cast<LRESULT>(backgroundBrush);
    }
    case WM_DPICHANGED: {
        UINT next = HIWORD(w);
        double ratio = double(next) / dpi;
        EnumChildWindows(
            hwnd,
            [](HWND child, LPARAM value) -> BOOL {
                double scale = *reinterpret_cast<double*>(value);
                RECT r{};
                GetWindowRect(child, &r);
                MapWindowPoints(nullptr, GetParent(child), reinterpret_cast<POINT*>(&r), 2);
                SetWindowPos(child, nullptr, int(r.left * scale), int(r.top * scale),
                             int((r.right - r.left) * scale), int((r.bottom - r.top) * scale), SWP_NOZORDER);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&ratio));
        HFONT old = font;
        font = CreateFontW(-MulDiv(16, next, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0,
                           0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        EnumChildWindows(
            hwnd,
            [](HWND child, LPARAM value) -> BOOL {
                SendMessageW(child, WM_SETFONT, value, TRUE);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(font));
        if (old)
            DeleteObject(old);
        dpi = next;
        if (titleFont)
            DeleteObject(titleFont);
        titleFont = CreateFontW(-MulDiv(27, next, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        auto r = reinterpret_cast<RECT*>(l);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        BuildCards();
        return 0;
    }
    case WM_COMMAND:
        try {
            int id = LOWORD(w);
            if (id == ScreenSelector && HIWORD(w) == CBN_SELCHANGE)
                RefreshSettings();
            int selected = static_cast<int>(SendMessageW(screens, CB_GETCURSEL, 0, 0));
            if (id == ChooseVideoButton && selected >= 0 && static_cast<size_t>(selected) < displays.size())
                ChooseV0Screen(displays[selected]);
            if (id == RemoveVideoButton && selected >= 0 && static_cast<size_t>(selected) < displays.size() &&
                !importing) {
                if (auto t = Target(displays[selected].device)) {
                    Stop(*t);
                    t->options.video.clear();
                    t->options.source.clear();
                    t->previousVideo.clear();
                    ShowWindow(t->window, SW_HIDE);
                    SaveScreens();
                    RefreshSettings();
                }
            }
            if (id == PauseButton) {
                application->manualPause = !application->manualPause;
                ForTargets(UpdatePause);
                RefreshSettings();
            }
            if (id == RefreshScreensButton)
                RefreshSettings(true);
            if (id == QuitButton)
                PostMessageW(application->window, WM_CLOSE, 0, 0);
            if (id == CancelImportButton)
                CancelImport();
            if (id == ScalingSelector && HIWORD(w) == CBN_SELCHANGE) {
                bool fill = SendMessageW(reinterpret_cast<HWND>(l), CB_GETCURSEL, 0, 0) == 0;
                ForTargets([&] {
                    Stop(*state);
                    state->options.fill = fill;
                    StartTarget(*state);
                });
                SaveScreens();
            }
            if (id == ImportProfileSelector && HIWORD(w) == CBN_SELCHANGE) {
                application->options.importFps =
                    SendMessageW(reinterpret_cast<HWND>(l), CB_GETCURSEL, 0, 0) == 0 ? 30 : 0;
                Save(application->options);
            }
        } catch (Failure f) {
            Error(f);
        } catch (...) {
            Error({E_FAIL, L"Préparation ou affectation impossible"});
        }
        return 0;
    case WM_CLOSE:
        ReleasePreviews();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        ReleasePreviews();
        cards.clear();
        panel = nullptr;
        screens = path = status = memory = progressBar = nullptr;
        if (font) {
            DeleteObject(font);
            font = nullptr;
        }
        if (titleFont) {
            DeleteObject(titleFont);
            titleFont = nullptr;
        }
        if (backgroundBrush) {
            DeleteObject(backgroundBrush);
            backgroundBrush = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
} // namespace v0
void RefreshSettings(bool rebuild) {
    using namespace v0;
    if (!panel)
        return;
    if (rebuild) {
        std::wstring previous;
        int old = static_cast<int>(SendMessageW(screens, CB_GETCURSEL, 0, 0));
        if (old >= 0 && static_cast<size_t>(old) < displays.size())
            previous = displays[old].device;
        displays = Screens();
        SendMessageW(screens, CB_RESETCONTENT, 0, 0);
        int choice = 0;
        for (size_t i = 0; i < displays.size(); ++i) {
            const auto& s = displays[i];
            auto label = s.device + L" — " + std::to_wstring(s.rect.right - s.rect.left) + L" × " +
                         std::to_wstring(s.rect.bottom - s.rect.top) + (s.primary ? L" (principal)" : L"");
            SendMessageW(screens, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            if (s.device == previous)
                choice = static_cast<int>(i);
        }
        SendMessageW(screens, CB_SETCURSEL, choice, 0);
        BuildCards();
    }
    int selected = static_cast<int>(SendMessageW(screens, CB_GETCURSEL, 0, 0));
    State* t = selected >= 0 && static_cast<size_t>(selected) < displays.size()
                   ? Target(displays[selected].device)
                   : nullptr;
    SetWindowTextW(path,
                   t ? (t->options.source.empty() ? t->options.video : t->options.source).c_str() : L"");
    SetWindowTextW(status, message.c_str());
    EnableWindow(GetDlgItem(panel, ChooseVideoButton), !importing);
    EnableWindow(GetDlgItem(panel, RemoveVideoButton), !importing);
    EnableWindow(GetDlgItem(panel, CancelImportButton), !!importing);
    SetWindowTextW(GetDlgItem(panel, PauseButton), application->manualPause ? L"Reprendre" : L"Pause");
    ShowWindow(progressBar, importing ? SW_SHOW : SW_HIDE);
    if (progressBar)
        SendMessageW(progressBar, PBM_SETPOS, importing ? importing->lastPercent : 0, 0);
    BindPreviews();
    InvalidateRect(panel, nullptr, FALSE);
}
void OpenSettings() {
    using namespace v0;
    if (panel) {
        ShowWindow(panel, SW_SHOW);
        SetForegroundWindow(panel);
        return;
    }
    backgroundBrush = CreateSolidBrush(Background);
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&common);
    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"PlumeV0.Settings";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_PLUME));
    RegisterClassW(&wc);
    panel = CreateWindowExW(0, wc.lpszClassName, L"Plume " PLUME_VERSION L" — Vidéos et écrans",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, 650, 390, nullptr, nullptr, wc.hInstance, nullptr);
    dpi = GetDpiForWindow(panel);
    SetWindowPos(panel, nullptr, 0, 0, MulDiv(650, dpi, 96), MulDiv(390, dpi, 96), SWP_NOMOVE | SWP_NOZORDER);
    font = CreateFontW(-MulDiv(16, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    titleFont = CreateFontW(-MulDiv(27, dpi, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto control = [&](const wchar_t* cls, const wchar_t* label, DWORD style, int x, int y, int width,
                       int height, int id) {
        HWND h = CreateWindowExW(0, cls, label, WS_CHILD | WS_VISIBLE | style, MulDiv(x, dpi, 96),
                                 MulDiv(y, dpi, 96), MulDiv(width, dpi, 96), MulDiv(height, dpi, 96), panel,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), wc.hInstance, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return h;
    };
    screens = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 20, 45, 595, 180,
                      ScreenSelector);
    path = control(L"EDIT", L"", ES_READONLY | ES_AUTOHSCROLL, 20, 112, 595, 28, VideoPathField);
    control(L"BUTTON", L"Choisir une vidéo…", WS_TABSTOP | BS_OWNERDRAW, 20, 155, 185, 32, ChooseVideoButton);
    control(L"BUTTON", L"Retirer de cet écran", WS_TABSTOP | BS_OWNERDRAW, 215, 155, 185, 32,
            RemoveVideoButton);
    control(L"BUTTON", L"Annuler l’import", WS_TABSTOP | BS_OWNERDRAW, 410, 155, 205, 32, CancelImportButton);
    control(L"BUTTON", L"Pause", WS_TABSTOP | BS_OWNERDRAW, 20, 204, 100, 30, PauseButton);
    control(L"BUTTON", L"Actualiser écrans", WS_TABSTOP | BS_OWNERDRAW, 130, 204, 160, 30,
            RefreshScreensButton);
    HWND scaling =
        control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 305, 204, 180, 120, ScalingSelector);
    SendMessageW(scaling, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Remplir l’écran"));
    SendMessageW(scaling, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Vidéo entière"));
    SendMessageW(scaling, CB_SETCURSEL, application->options.fill ? 0 : 1, 0);
    control(L"BUTTON", L"Quitter", WS_TABSTOP | BS_OWNERDRAW, 500, 204, 115, 30, QuitButton);
    status = control(L"STATIC", L"", 0, 20, 252, 595, 48, StatusLabel);
    memory = control(L"STATIC", L"", 0, 20, 311, 595, 24, MemoryLabel);
    WNDCLASSW progressClass{};
    progressClass.lpfnWndProc = ProgressProc;
    progressClass.hInstance = wc.hInstance;
    progressClass.lpszClassName = L"PlumeV0.Progress";
    RegisterClassW(&progressClass);
    progressBar = control(progressClass.lpszClassName, L"", 0, 0, 0, 1, 1, ImportProgress);
    SendMessageW(progressBar, PBM_SETRANGE32, 0, 100);
    control(L"STATIC", L"Prochains imports", 0, 0, 0, 1, 1, ImportProfileLabel);
    HWND importProfile =
        control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 0, 0, 1, 1, ImportProfileSelector);
    SendMessageW(importProfile, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"30 images/s · consommation réduite"));
    SendMessageW(importProfile, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Conserver la cadence d’origine"));
    SendMessageW(importProfile, CB_SETCURSEL, application->options.importFps ? 0 : 1, 0);
    RefreshSettings(true);
    ShowWindow(panel, application->uiSmokeTest ? SW_HIDE : SW_SHOW);
    if (!application->uiSmokeTest)
        SetForegroundWindow(panel);
    if (application->uiSmokeTest) {
        Check(SendMessageW(screens, CB_GETCOUNT, 0, 0) == static_cast<LRESULT>(Screens().size()) ? S_OK
                                                                                                 : E_FAIL,
              L"UI : liste écrans");
        Log(L"UI smoke : liste écrans et contrôles créés");
    }
}
void ChooseV0Screen(const Screen& screen) {
    if (v0::importing)
        return;
    wchar_t file[32768]{};
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = v0::panel;
    dialog.lpstrFile = file;
    dialog.nMaxFile = 32768;
    dialog.lpstrFilter = L"Vidéos\0*.mp4;*.mov;*.mkv;*.avi;*.webm;*.m4v;*.wmv\0Tous les fichiers\0*.*\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog))
        v0::ImportVideo(file, screen.device);
}
void TickSettings() {
    using namespace v0;
    if (application->openSettingsRequest.value &&
        WaitForSingleObject(application->openSettingsRequest.value, 0) == WAIT_OBJECT_0)
        OpenSettings();
    UpdateImportProgress();
    PollImport();
    if (panel) {
        PROCESS_MEMORY_COUNTERS counters{sizeof(counters)};
        GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters));
        wchar_t label[120]{};
        swprintf_s(label, L"RAM actuelle du programme : %.1f Mo — interface comprise",
                   counters.WorkingSetSize / 1048576.0);
        SetWindowTextW(memory, label);
    }
}
void HandleV0Error(State& target) {
    v0::Stop(target);
    auto failed = target.error;
    target.options.video = std::move(target.previousVideo);
    target.options.source = std::move(target.previousSource);
    target.previousVideo.clear();
    target.previousSource.clear();
    target.error = {S_OK, L""};
    if (target.options.video.empty())
        ShowWindow(target.window, SW_HIDE);
    else
        StartTarget(target);
    SaveScreens();
    OpenSettings();
    v0::Error(failed);
    RefreshSettings();
}
void CleanupV0() {
    v0::ShutdownImport();
    if (v0::panel)
        DestroyWindow(v0::panel);
}

HWND SettingsWindow() {
    return v0::panel;
}
void PrepareScreen(const std::wstring& source, const std::wstring& device) {
    v0::ImportVideo(source, device);
}
void ShowSettingsError(Failure failure) {
    v0::Error(failure);
}
