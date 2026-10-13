#include "app_state.h"
#include "ui/settings.h"

void SaveScreens() {
    const auto& ini = application->options.ini;
    if (application->monitorDevice.empty() &&
        !WritePrivateProfileStringW(L"screens", nullptr, nullptr, ini.c_str()))
        throw Failure{E_FAIL, L"Sauvegarde écrans"};
    auto write = [&](State& target) {
        if constexpr (V0Build)
            if (!target.monitorDevice.empty())
                WritePrivateProfileStringW(L"sources", target.monitorDevice.c_str(),
                                           target.options.source.c_str(), ini.c_str());
        if (!target.monitorDevice.empty() &&
            !WritePrivateProfileStringW(L"screens", target.monitorDevice.c_str(),
                                        target.options.video.c_str(), ini.c_str()))
            throw Failure{E_FAIL, L"Sauvegarde vidéo par écran"};
    };
    write(*application);
    for (auto& child : application->children)
        write(*child);
    Save(application->options);
}
void ChooseScreen(const Screen& screen) {
    if constexpr (V0Build) {
        ChooseV0Screen(screen);
        return;
    }
    wchar_t file[32768]{};
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.lpstrFile = file;
    dialog.nMaxFile = 32768;
    dialog.lpstrTitle = L"Choisir la vidéo de cet écran";
    dialog.lpstrFilter = L"Vidéos\0*.mp4;*.mov;*.m4v;*.wmv;*.avi;*.mkv\0Tous les fichiers\0*.*\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog))
        return;
    if (application->monitorDevice.empty()) {
        application->options.span = false;
        for (const auto& item : Screens())
            if (item.primary)
                application->monitorDevice = item.device;
        State* previous = state;
        state = application;
        AttachDesktop();
        state = previous;
    }
    State* target = application->monitorDevice == screen.device ? application : nullptr;
    for (auto& child : application->children)
        if (child->monitorDevice == screen.device)
            target = child.get();
    if (!target) {
        auto child = std::make_unique<State>();
        child->root = application;
        child->options = application->options;
        child->options.span = false;
        child->monitorDevice = screen.device;
        child->options.video = file;
        target = child.get();
        application->children.push_back(std::move(child));
        CreateTargetWindow(*target);
    } else {
        target->stop = true;
        SetEvent(target->wake.value);
        if (target->playback.joinable())
            target->playback.join();
        target->options.video = file;
    }
    StartTarget(*target);
    SaveScreens();
}

std::wstring ConfigPath() {
    wchar_t base[32768]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, 32768);
    if (!n || n >= 32768)
        throw Failure{E_FAIL, L"LOCALAPPDATA"};
    std::wstring dir = std::wstring(base) + (V0Build ? L"\\PlumeWallpaperV0" : L"\\PlumeWallpaper");
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        throw Failure{HRESULT_FROM_WIN32(GetLastError()), L"Créer dossier configuration"};
    std::wstring path = dir + L"\\settings.ini";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        WORD bom = 0xFEFF;
        DWORD written = 0;
        WriteFile(file, &bom, sizeof(bom), &written, nullptr);
        CloseHandle(file);
    }
    return path;
}
void Save(const Options& o) {
    if constexpr (V0Build)
        WritePrivateProfileStringW(L"wallpaper", L"importFps", o.importFps ? L"30" : L"0", o.ini.c_str());
    if (!WritePrivateProfileStringW(L"wallpaper", L"video", o.video.c_str(), o.ini.c_str()) ||
        !WritePrivateProfileStringW(L"wallpaper", L"scaling", o.fill ? L"fill" : L"fit", o.ini.c_str()) ||
        !WritePrivateProfileStringW(L"wallpaper", L"monitors", o.span ? L"span" : L"primary",
                                    o.ini.c_str()) ||
        !WritePrivateProfileStringW(L"wallpaper", L"battery", o.battery ? L"1" : L"0", o.ini.c_str()) ||
        !WritePrivateProfileStringW(L"wallpaper", L"compat", o.compat ? L"1" : L"0", o.ini.c_str()))
        throw Failure{HRESULT_FROM_WIN32(GetLastError()), L"Sauvegarder INI"};
}
void AutoStart(bool enable) {
    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                     0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS)
        throw Failure{HRESULT_FROM_WIN32(status), L"Registre autostart"};
    if (enable) {
        wchar_t path[32768]{};
        GetModuleFileNameW(nullptr, path, 32768);
        std::wstring command = L"\"" + std::wstring(path) + L"\"";
        status = RegSetValueExW(key, V0Build ? L"PlumeWallpaperV0" : L"PlumeWallpaper", 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(command.c_str()),
                                DWORD((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, V0Build ? L"PlumeWallpaperV0" : L"PlumeWallpaper");
        if (status == ERROR_FILE_NOT_FOUND)
            status = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    Check(HRESULT_FROM_WIN32(status), L"Modifier autostart");
}
