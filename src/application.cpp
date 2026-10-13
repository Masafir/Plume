#include "app_state.h"
#include "ui/settings.h"

thread_local State* state = nullptr;
State* application = nullptr;

int RunApplication(HINSTANCE instance) {
    State app;
    state = &app;
    application = &app;
    int result = 0;
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        Check(com, L"COM UI");
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv)
            throw Failure{E_OUTOFMEMORY, L"Arguments"};
        struct ArgGuard {
            wchar_t** p;
            ~ArgGuard() {
                LocalFree(p);
            }
        } guard{argv};
        // These commands update only the user's Run key, including while Plume is already running.
        if (argc == 2 && (wcscmp(argv[1], L"--autostart") == 0 || wcscmp(argv[1], L"--no-autostart") == 0)) {
            AutoStart(wcscmp(argv[1], L"--autostart") == 0);
            return 0;
        }
        bool quit = false;
        int autostart = -1;
        UINT testSeconds = 0;
        std::wstring reportPath;
        std::vector<std::pair<std::wstring, std::wstring>> assignments;
        // Commands that do not need config or a video.
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"--help") == 0) {
                MessageBoxW(nullptr,
                            L"PlumeWallpaper.exe [vidéo] [--fit|--fill] [--primary|--span]\n"
                            L"[--screen DISPLAY1 vidéo] [--screen DISPLAY2 autre-vidéo] [--list-screens]\n"
                            L"[--compat] [--strict] [--on-battery|--pause-on-battery]\n"
                            L"[--autostart|--no-autostart] [--quit]\n\n"
                            L"Diagnostic : --test-seconds N --report fichier --audit-memory\n"
                            L"Expérimental : --decoder-samples N (1-32) --flush-loop\n\n"
                            L"Prototype : --bounded (MP4 H264 préparé sans B-frames)\n\n"
                            L"Comparaison : --bounded-low-latency (avec --bounded)\n\n"
                            L"Par défaut : matériel strict (Windows 11 25H2), fill, écran principal, pause "
                            L"sur batterie.\n"
                            L"Clic droit sur l'icône de notification : pause / quitter.",
                            L"Plume — usage", MB_OK);
                return 0;
            }
            if (wcscmp(argv[i], L"--list-screens") == 0) {
                std::wstring text;
                for (const auto& screen : Screens())
                    text += screen.device + L" : " + std::to_wstring(screen.rect.right - screen.rect.left) +
                            L" × " + std::to_wstring(screen.rect.bottom - screen.rect.top) +
                            (screen.primary ? L" (principal)" : L"") + L"\n";
                MessageBoxW(nullptr, text.c_str(), L"Plume — écrans actifs", MB_OK);
                return 0;
            }
            if (wcscmp(argv[i], L"--quit") == 0)
                quit = true;
        }
        if (quit) {
            std::vector<DWORD> processes;
            // The rendering window is a child, so FindWindow alone is insufficient.
            EnumWindows(
                [](HWND top, LPARAM data) -> BOOL {
                    EnumChildWindows(
                        top,
                        [](HWND child, LPARAM data) -> BOOL {
                            wchar_t name[64]{};
                            GetClassNameW(child, name, 64);
                            if (wcscmp(name, ClassName) == 0) {
                                DWORD pid = 0;
                                GetWindowThreadProcessId(child, &pid);
                                auto& ids = *reinterpret_cast<std::vector<DWORD>*>(data);
                                if (pid && std::find(ids.begin(), ids.end(), pid) == ids.end())
                                    ids.push_back(pid);
                                PostMessageW(child, WM_CLOSE, 0, 0);
                            }
                            return TRUE;
                        },
                        data);
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&processes));
            UINT64 deadline = GetTickCount64() + 10000;
            for (DWORD pid : processes) {
                Handle process;
                process.value = OpenProcess(SYNCHRONIZE, FALSE, pid);
                if (process.value) {
                    DWORD remaining = DWORD(deadline > GetTickCount64() ? deadline - GetTickCount64() : 0);
                    if (WaitForSingleObject(process.value, remaining) == WAIT_TIMEOUT)
                        return ERROR_TIMEOUT;
                }
            }
            return 0;
        }
        Handle mutex;
        mutex.value = CreateMutexW(nullptr, FALSE,
                                   V0Build ? L"Local\\PlumeWallpaper.V0.SingleInstance"
                                           : L"Local\\PlumeWallpaper.SingleInstance");
        if (!mutex.value)
            throw Failure{HRESULT_FROM_WIN32(GetLastError()), L"Mutex"};
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            if constexpr (V0Build) {
                HANDLE request =
                    OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\PlumeWallpaper.V0.OpenSettings");
                if (request) {
                    SetEvent(request);
                    CloseHandle(request);
                }
                return 0;
            }
            throw Failure{HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS),
                          L"Déjà lancé ; utiliser --quit avant de changer la vidéo"};
        }
        if constexpr (V0Build)
            app.openSettingsRequest.value =
                CreateEventW(nullptr, FALSE, FALSE, L"Local\\PlumeWallpaper.V0.OpenSettings");
        app.options.ini = ConfigPath();
        wchar_t saved[32768]{};
        GetPrivateProfileStringW(L"wallpaper", L"video", L"", saved, 32768, app.options.ini.c_str());
        app.options.video = saved;
        GetPrivateProfileStringW(L"wallpaper", L"scaling", L"fill", saved, 32768, app.options.ini.c_str());
        app.options.fill = wcscmp(saved, L"fit") != 0;
        GetPrivateProfileStringW(L"wallpaper", L"monitors", L"primary", saved, 32768,
                                 app.options.ini.c_str());
        app.options.span = wcscmp(saved, L"span") == 0;
        app.options.battery =
            GetPrivateProfileIntW(L"wallpaper", L"battery", 1, app.options.ini.c_str()) != 0;
        app.options.compat = GetPrivateProfileIntW(L"wallpaper", L"compat", 0, app.options.ini.c_str()) != 0;
        if constexpr (V0Build)
            app.options.importFps =
                GetPrivateProfileIntW(L"wallpaper", L"importFps", 30, app.options.ini.c_str()) ? 30 : 0;
        for (const auto& screen : Screens()) {
            GetPrivateProfileStringW(L"screens", screen.device.c_str(), L"", saved, 32768,
                                     app.options.ini.c_str());
            if (*saved)
                assignments.emplace_back(screen.device, saved);
        }
        bool specifiedScreens = false;
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"--fit")
                app.options.fill = false;
            else if (arg == L"--fill")
                app.options.fill = true;
            else if (arg == L"--span") {
                app.options.span = true;
                assignments.clear();
            } else if (arg == L"--primary") {
                app.options.span = false;
                assignments.clear();
            } else if (arg == L"--screen") {
                if (i + 2 >= argc)
                    throw Failure{E_INVALIDARG, L"--screen DISPLAYn chemin-vidéo"};
                if (!specifiedScreens) {
                    assignments.clear();
                    specifiedScreens = true;
                }
                std::wstring name = argv[++i];
                std::wstring path = argv[++i];
                if (!name.starts_with(L"\\\\.\\"))
                    name = L"\\\\.\\" + name;
                bool exists = false;
                for (const auto& screen : Screens())
                    if (screen.device == name)
                        exists = true;
                if (!exists)
                    throw Failure{E_INVALIDARG, L"Écran inactif/inconnu ; --list-screens"};
                for (const auto& item : assignments)
                    if (item.first == name)
                        throw Failure{E_INVALIDARG, L"Écran affecté deux fois"};
                assignments.emplace_back(name, path);
                app.options.span = false;
            } else if (arg == L"--compat")
                app.options.compat = true;
            else if (arg == L"--strict")
                app.options.compat = false;
            else if (arg == L"--on-battery")
                app.options.battery = false;
            else if (arg == L"--pause-on-battery")
                app.options.battery = true;
            else if (arg == L"--autostart")
                autostart = 1;
            else if (arg == L"--no-autostart")
                autostart = 0;
            else if (arg == L"--test-seconds") {
                if (++i >= argc)
                    throw Failure{E_INVALIDARG, L"Durée de test manquante"};
                wchar_t* end = nullptr;
                unsigned long duration = wcstoul(argv[i], &end, 10);
                if (*end || duration < 1 || duration > 86400)
                    throw Failure{E_INVALIDARG, L"Durée de test : 1 à 86400 secondes"};
                testSeconds = UINT(duration);
                app.testMode = true;
            } else if (arg == L"--report") {
                if (++i >= argc)
                    throw Failure{E_INVALIDARG, L"Chemin du rapport manquant"};
                reportPath = argv[i];
            } else if (arg == L"--audit-memory")
                app.auditMemory = true;
            else if (arg == L"--audit-no-render")
                app.auditNoRender = true;
            else if (arg == L"--flush-loop")
                app.flushLoop = true;
            else if (arg == L"--bounded")
                app.bounded = true;
            else if (arg == L"--bounded-low-latency")
                app.boundedLowLatency = true;
            else if (arg == L"--ui-smoke-test")
                app.uiSmokeTest = true;
            else if (arg == L"--show-settings")
                app.showSettings = true;
            else if (arg == L"--import-test") {
                if (++i >= argc)
                    throw Failure{E_INVALIDARG, L"Vidéo de test import manquante"};
                app.importTest = argv[i];
            } else if (arg == L"--prepare-screen") {
                if (i + 2 >= argc)
                    throw Failure{E_INVALIDARG, L"--prepare-screen DISPLAYn vidéo"};
                app.importScreen = argv[++i];
                if (!app.importScreen.starts_with(L"\\\\.\\"))
                    app.importScreen = L"\\\\.\\" + app.importScreen;
                app.importTest = argv[++i];
            } else if (arg == L"--decoder-samples") {
                if (++i >= argc)
                    throw Failure{E_INVALIDARG, L"Minimum de sortie manquant"};
                wchar_t* end = nullptr;
                unsigned long count = wcstoul(argv[i], &end, 10);
                if (*end || count < 1 || count > 32)
                    throw Failure{E_INVALIDARG, L"Minimum de sortie : 1 à 32"};
                app.decoderSamples = UINT(count);
            } else if (arg.starts_with(L"--"))
                throw Failure{E_INVALIDARG, L"Argument inconnu ; --help"};
            else {
                app.options.video = arg;
                assignments.clear();
            }
        }
        if (autostart == 0) {
            AutoStart(false);
            return 0;
        }
        if (app.auditNoRender && (!app.auditMemory || !app.testMode))
            throw Failure{E_INVALIDARG, L"--audit-no-render requiert --audit-memory et --test-seconds"};
        if (app.boundedLowLatency && !app.bounded)
            throw Failure{E_INVALIDARG, L"--bounded-low-latency requiert --bounded"};
        if constexpr (V0Build)
            if (reportPath.empty()) {
                reportPath =
                    app.options.ini.substr(0, app.options.ini.find_last_of(L"\\/")) + L"\\session.log";
            }
        if (!reportPath.empty()) {
            app.report = _wfsopen(reportPath.c_str(), L"w, ccs=UTF-8", _SH_DENYNO);
            if (!app.report)
                throw Failure{E_FAIL, L"Créer rapport"};
            Log(L"Démarrage PlumeWallpaper");
            Audit(L"baseline", true);
        }
        if (!assignments.empty()) {
            app.monitorDevice = assignments.front().first;
            app.options.video = assignments.front().second;
        }
        if constexpr (V0Build) {
            GetPrivateProfileStringW(L"sources", app.monitorDevice.c_str(), L"", saved, 32768,
                                     app.options.ini.c_str());
            app.options.source = saved;
        }
        if (app.options.video.empty() && !V0Build) {
            wchar_t file[32768]{};
            OPENFILENAMEW dialog{sizeof(dialog)};
            dialog.lpstrFile = file;
            dialog.nMaxFile = 32768;
            dialog.lpstrFilter = L"Vidéos\0*.mp4;*.mov;*.m4v;*.wmv;*.avi;*.mkv\0Tous les fichiers\0*.*\0";
            dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (!GetOpenFileNameW(&dialog))
                return 0;
            app.options.video = file;
        }
        wchar_t absolute[32768]{};
        if constexpr (V0Build) {
            app.options.span = false;
            if (app.monitorDevice.empty())
                for (const auto& screen : Screens())
                    if (screen.primary)
                        app.monitorDevice = screen.device;
            GetPrivateProfileStringW(L"sources", app.monitorDevice.c_str(), L"", saved, 32768,
                                     app.options.ini.c_str());
            app.options.source = saved;
            GetPrivateProfileStringW(L"screens", app.monitorDevice.c_str(), L"<unset>", saved, 32768,
                                     app.options.ini.c_str());
            if (app.options.video.empty() && wcscmp(saved, L"<unset>") == 0) {
                wchar_t exe[32768]{};
                GetModuleFileNameW(nullptr, exe, 32768);
                std::wstring demo = std::wstring(exe);
                demo.resize(demo.find_last_of(L"\\/") + 1);
                demo += L"bojji-4k-h264-lowmemory.mp4";
                if (GetFileAttributesW(demo.c_str()) != INVALID_FILE_ATTRIBUTES)
                    app.options.video = demo;
            }
        }
        if (!app.options.video.empty()) {
            DWORD n = GetFullPathNameW(app.options.video.c_str(), 32768, absolute, nullptr);
            if (!n || n >= 32768)
                throw Failure{E_INVALIDARG, L"Chemin vidéo"};
            app.options.video = absolute;
            DWORD attrs = GetFileAttributesW(absolute);
            if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                if constexpr (V0Build) {
                    if (!app.testMode)
                        app.options.video.clear();
                    else
                        throw Failure{HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),
                                      L"Fichier vidéo local introuvable"};
                } else
                    throw Failure{HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),
                                  L"Fichier vidéo local introuvable"};
            }
        }
        WNDCLASSW wc{};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = ClassName;
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_PLUME));
        Check(RegisterClassW(&wc) ? S_OK : HRESULT_FROM_WIN32(GetLastError()), L"RegisterClass");
        app.taskbarMessage = RegisterWindowMessageW(L"TaskbarCreated");
        CreateTargetWindow(app);
        for (size_t i = 1; i < assignments.size(); ++i) {
            auto target = std::make_unique<State>();
            target->root = &app;
            target->options = app.options;
            target->monitorDevice = assignments[i].first;
            target->options.span = false;
            DWORD length = GetFullPathNameW(assignments[i].second.c_str(), 32768, absolute, nullptr);
            if (!length || length >= 32768 || GetFileAttributesW(absolute) == INVALID_FILE_ATTRIBUTES) {
                if constexpr (V0Build) {
                    if (!app.testMode)
                        continue;
                }
                throw Failure{HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), L"Vidéo d'écran introuvable"};
            }
            if constexpr (V0Build) {
                GetPrivateProfileStringW(L"sources", target->monitorDevice.c_str(), L"", saved, 32768,
                                         app.options.ini.c_str());
                target->options.source = saved;
            }
            target->options.video = absolute;
            app.children.push_back(std::move(target));
            CreateTargetWindow(*app.children.back());
        }
        WTSRegisterSessionNotification(app.window, NOTIFY_FOR_THIS_SESSION);
        app.timer = SetTimer(nullptr, 0, 1000, [](HWND, UINT, UINT_PTR, DWORD) { Tick(); });
        if (!app.timer)
            throw Failure{HRESULT_FROM_WIN32(GetLastError()), L"Timer de cycle de vie"};
        UpdatePause();
        Tray();
        Audit(L"after-desktop-UI");
        if (testSeconds)
            app.deadline = GetTickCount64() + UINT64(testSeconds) * 1000;
        SaveScreens();
        if (autostart == 1)
            AutoStart(true);
        StartTarget(app);
        for (auto& target : app.children)
            StartTarget(*target);
        if constexpr (V0Build)
            if (!app.testMode || app.uiSmokeTest || app.showSettings)
                OpenSettings();
        if constexpr (V0Build)
            if (!app.importTest.empty()) {
                if (!app.testMode && app.importScreen.empty())
                    throw Failure{E_INVALIDARG, L"--import-test requiert --test-seconds"};
                try {
                    bool active = app.importScreen.empty();
                    for (const auto& screen : Screens())
                        if (screen.device == app.importScreen)
                            active = true;
                    Check(active ? S_OK : E_INVALIDARG, L"Écran de préparation inactif");
                    PrepareScreen(app.importTest,
                                  app.importScreen.empty() ? app.monitorDevice : app.importScreen);
                } catch (Failure f) {
                    ShowSettingsError(f);
                }
            }
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if constexpr (V0Build)
                if (SettingsWindow() && IsDialogMessageW(SettingsWindow(), &msg))
                    continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    } catch (Failure f) {
        app.error = f;
    } catch (...) {
        app.error = {E_UNEXPECTED, L"Erreur inattendue"};
    }
    StopAll();
    if constexpr (V0Build)
        CleanupV0();
    if (app.timer)
        KillTimer(nullptr, app.timer);
    if (app.playback.joinable())
        app.playback.join();
    for (auto& target : app.children) {
        if (target->playback.joinable())
            target->playback.join();
        if (FAILED(target->error.hr) && SUCCEEDED(app.error.hr))
            app.error = target->error;
        if (target->window)
            DestroyWindow(target->window);
    }
    if (app.window) {
        Tray(true);
        WTSUnRegisterSessionNotification(app.window);
        KillTimer(app.window, 1);
        DestroyWindow(app.window);
    }
    Audit(L"after-readers-renderers-release", true);
    app.gpu.reset();
    Audit(L"after-D3D11-release", true);
    if (app.report) {
        wchar_t text[512]{};
        auto report = [&](State& target) {
            swprintf_s(text, L"Résultat %s : samples=%llu frames=%llu HRESULT=0x%08X",
                       target.monitorDevice.c_str(), target.samples.load(), target.frames.load(),
                       unsigned(target.error.hr));
            Log(text);
        };
        report(app);
        for (auto& target : app.children)
            report(*target);
        swprintf_s(text, L"Pic working set échantillonné=%.2f MiB ; pic private bytes=%.2f MiB",
                   double(app.peakWorkingSet) / 1048576, double(app.peakPrivateBytes) / 1048576);
        Log(text);
        FILETIME created{}, exited{}, kernel{}, user{};
        if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
            auto ticks = [](FILETIME t) { return (UINT64(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
            double wallSeconds = double(GetTickCount64() - app.startedTick) / 1000;
            double cpuSeconds = double(ticks(kernel) + ticks(user)) / 10000000;
            swprintf_s(text, L"CPU moyen sur un cœur=%.2f %% ; durée=%.2f s",
                       wallSeconds > 0 ? cpuSeconds * 100 / wallSeconds : 0, wallSeconds);
            Log(text);
        }
        fclose(app.report);
        app.report = nullptr;
    }
    if (FAILED(app.error.hr)) {
        wchar_t text[1024]{};
        swprintf_s(text,
                   L"%s\nHRESULT : 0x%08X\n\nConsulter README.md pour codecs, mode --compat et compilation.",
                   app.error.operation, unsigned(app.error.hr));
        if (!app.testMode)
            MessageBoxW(nullptr, text, L"Plume — erreur", MB_OK | MB_ICONERROR);
        result = 1;
    }
    if (SUCCEEDED(com))
        CoUninitialize();
    state = nullptr;
    return result;
}
