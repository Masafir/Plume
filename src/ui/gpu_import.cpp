#include "settings_state.h"
#include "../video/bounded_mp4.h"
#include <filesystem>

namespace v0 {
std::unique_ptr<Import> importing;
std::string ReadProgressText(const std::wstring& filePath, bool tail) {
    Handle file;
    file.value =
        CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file.value == INVALID_HANDLE_VALUE) {
        file.value = nullptr;
        return {};
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.value, &size))
        return {};
    constexpr DWORD limit = 65536;
    DWORD count = DWORD(std::min<LONGLONG>(size.QuadPart, limit));
    if (tail && size.QuadPart > limit) {
        LARGE_INTEGER position{};
        position.QuadPart = size.QuadPart - limit;
        SetFilePointerEx(file.value, position, nullptr, FILE_BEGIN);
    }
    std::string result(count, '\0');
    DWORD read = 0;
    if (!ReadFile(file.value, result.data(), count, &read, nullptr))
        return {};
    result.resize(read);
    return result;
}
void UpdateImportProgress() {
    if (!importing)
        return;
    auto& work = *importing;
    if (work.cancelled)
        return;
    if (!work.durationSeconds) {
        auto log = ReadProgressText(work.log, false);
        auto start = log.find("Duration: ");
        if (start != std::string::npos) {
            unsigned hours = 0, minutes = 0;
            double seconds = 0;
            if (sscanf_s(log.c_str() + start + 10, "%u:%u:%lf", &hours, &minutes, &seconds) == 3)
                work.durationSeconds = hours * 3600.0 + minutes * 60.0 + seconds;
        }
    }
    auto data = ReadProgressText(work.progressFile, true);
    auto time = data.rfind("out_time_us=");
    double encoded = time == std::string::npos ? 0 : strtod(data.c_str() + time + 12, nullptr) / 1000000.0;
    double elapsed = (GetTickCount64() - work.started) / 1000.0;
    std::wstring screen = work.device;
    for (size_t i = 0; i < displays.size(); ++i)
        if (displays[i].device == work.device)
            screen = L"écran " + std::to_wstring(i + 1);
    wchar_t detail[160]{};
    if (work.durationSeconds > 0) {
        double ratio = std::clamp(encoded / work.durationSeconds, 0.0, 1.0);
        int percent = std::max(work.lastPercent, std::min(99, int(ratio * 100)));
        work.lastPercent = percent;
        if (progressBar && SendMessageW(progressBar, PBM_GETPOS, 0, 0) != percent)
            SendMessageW(progressBar, PBM_SETPOS, percent, 0);
        if (ratio >= 0.03 && elapsed >= 2) {
            swprintf_s(detail, L"%d %% · %.0f s écoulées · ≈ %.0f s restantes", percent, elapsed,
                       elapsed * (1 - ratio) / ratio);
        } else
            swprintf_s(detail, L"%d %% · %.0f s écoulées", percent, elapsed);
    } else {
        swprintf_s(detail, L"analyse de la durée · %.0f s écoulées", elapsed);
    }
    message = L"Préparation GPU pour " + screen + L" : " + detail;
    SetWindowTextW(status, message.c_str());
}
std::wstring Quote(const std::wstring& s) { // Windows argv quoting, including trailing backslashes.
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : s) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        out.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (c == L'"')
            out += L'\\';
        out += c;
    }
    out.append(slashes * 2, L'\\');
    out += L'"';
    return out;
}
std::wstring Ffmpeg() {
    // Prefer the explicitly installed tool next to Plume, never the caller's working directory.
    wchar_t module[32768]{};
    GetModuleFileNameW(nullptr, module, 32768);
    auto directory = std::filesystem::path(module).parent_path();
    for (const auto& candidate : {directory / L"tools" / L"ffmpeg.exe", directory / L"ffmpeg.exe"})
        if (std::filesystem::is_regular_file(candidate))
            return candidate.wstring();
    wchar_t found[32768]{};
    wchar_t searchPath[32768]{};
    DWORD pathLength = GetEnvironmentVariableW(L"PATH", searchPath, 32768);
    if (pathLength && pathLength < 32768 &&
        SearchPathW(searchPath, L"ffmpeg.exe", nullptr, 32768, found, nullptr))
        return found;
    wchar_t local[32768]{};
    GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
    std::filesystem::path packages = std::filesystem::path(local) / L"Microsoft/WinGet/Packages";
    std::error_code ec;
    for (const auto& pkg : std::filesystem::directory_iterator(packages, ec))
        if (pkg.path().filename().wstring().starts_with(L"Gyan.FFmpeg"))
            for (const auto& folder : std::filesystem::directory_iterator(pkg.path(), ec)) {
                auto exe = folder.path() / L"bin/ffmpeg.exe";
                if (std::filesystem::exists(exe))
                    return exe.wstring();
            }
    throw Failure{HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),
                  L"FFmpeg introuvable : installer FFmpeg pour préparer les vidéos sur le GPU"};
}
void ImportVideo(const std::wstring& source, const std::wstring& device) {
    if (importing)
        return;
    bool direct = false;
    try {
        BoundedMp4 compatible(source, 4);
        direct =
            !application->options.importFps || compatible.FrameRate() <= application->options.importFps + 0.5;
    } catch (Failure) {
    }
    if (direct) {
        Assign(device, source, source);
        message = L"Vidéo affectée à cet écran.";
        RefreshSettings();
        return;
    }
    auto exe = Ffmpeg();
    auto work = std::make_unique<Import>();
    work->source = source;
    work->device = device;
    auto cache = std::filesystem::path(application->options.ini).parent_path() / L"cache";
    std::filesystem::create_directories(cache);
    work->output = (cache / (L"video-" + std::to_wstring(GetTickCount64()) + L".mp4")).wstring();
    work->log = work->output + L".log";
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    Handle output, input;
    output.value = CreateFileW(work->log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    input.value =
        CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    if (output.value == INVALID_HANDLE_VALUE) {
        output.value = nullptr;
        throw Failure{E_FAIL, L"Créer journal import"};
    }
    if (input.value == INVALID_HANDLE_VALUE) {
        input.value = nullptr;
        throw Failure{E_FAIL, L"Ouvrir entrée import"};
    }
    work->progressFile = work->output + L".progress";
    std::wstring command =
        Quote(exe) + L" -hide_banner -nostdin -nostats -stats_period 0.5 -progress " +
        Quote(work->progressFile) +
        L" -n -protocol_whitelist file,pipe -hwaccel cuda -hwaccel_output_format cuda -i " + Quote(source) +
        L" -map 0:v:0 -an -c:v h264_nvenc -preset p6 -tune hq -rc vbr -cq 20 -b:v 0 -bf 0 "
        L"-refs 1 -pix_fmt cuda -movflags +faststart " +
        Quote(work->output);
    if (application->options.importFps) {
        auto outputPosition = command.rfind(Quote(work->output));
        command.insert(outputPosition, L"-r 30 -fps_mode cfr ");
    }
    // Restrict inheritance to the redirected streams, even if another component owns
    // inheritable handles. Never inherit the caller's working directory for DLL lookup.
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = input.value;
    si.StartupInfo.hStdOutput = output.value;
    si.StartupInfo.hStdError = output.value;
    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<unsigned char> attributes(attributeBytes);
    si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    Check(InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attributeBytes)
              ? S_OK
              : HRESULT_FROM_WIN32(GetLastError()),
          L"Initialiser attributs import");
    struct AttributeGuard {
        LPPROC_THREAD_ATTRIBUTE_LIST list;
        ~AttributeGuard() {
            DeleteProcThreadAttributeList(list);
        }
    } attributeGuard{si.lpAttributeList};
    HANDLE inherited[] = {input.value, output.value};
    Check(UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                    sizeof(inherited), nullptr, nullptr)
              ? S_OK
              : HRESULT_FROM_WIN32(GetLastError()),
          L"Limiter handles import");
    auto toolDirectory = std::filesystem::path(exe).parent_path().wstring();
    PROCESS_INFORMATION pi{};
    work->job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    Check(work->job && SetInformationJobObject(work->job, JobObjectExtendedLimitInformation, &limits,
                                               sizeof(limits))
              ? S_OK
              : E_FAIL,
          L"Limiter durée de vie import GPU");
    Check(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
                         CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                         toolDirectory.c_str(), &si.StartupInfo, &pi)
              ? S_OK
              : HRESULT_FROM_WIN32(GetLastError()),
          L"Lancer FFmpeg GPU");
    work->process = pi.hProcess;
    if (!AssignProcessToJobObject(work->job, pi.hProcess)) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        throw Failure{E_FAIL, L"Attacher import au processus"};
    }
    DWORD resumed = ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    Check(resumed != DWORD(-1) ? S_OK : E_FAIL, L"Démarrer import GPU");
    importing = std::move(work);
    message = L"Préparation GPU en cours… La vidéo précédente reste active.";
    Log(L"Import GPU démarré ; original intact");
    RefreshSettings();
}

void PollImport() {
    if (importing && WaitForSingleObject(importing->process, 0) == WAIT_OBJECT_0) {
        DWORD code = 1;
        GetExitCodeProcess(importing->process, &code);
        auto finished = std::move(importing);
        try {
            if (finished->cancelled) {
                DeleteFileW(finished->output.c_str());
                message = L"Préparation annulée ; vidéo précédente conservée.";
                Log(L"Import GPU annulé");
            } else {
                if (code)
                    throw Failure{E_FAIL, L"Préparation GPU échouée ; original conservé. Le journal se "
                                          L"trouve dans le dossier cache de PlumeWallpaperV0."};
                Assign(finished->device, finished->output, finished->source);
                message = L"Préparation terminée : 100 %. Vidéo affectée à l’écran.";
                Log(L"Import GPU terminé : MP4 validé et affecté");
            }
        } catch (Failure f) {
            DeleteFileW(finished->output.c_str());
            Error(f);
            Log(L"Import GPU échoué");
        } catch (...) {
            DeleteFileW(finished->output.c_str());
            Error({E_FAIL, L"Vidéo préparée incompatible"});
            Log(L"Import GPU échoué");
        }
        RefreshSettings();
    }
}

void CancelImport() {
    if (importing) {
        importing->cancelled = true;
        TerminateJobObject(importing->job, ERROR_CANCELLED);
        message = L"Annulation de la préparation…";
    }
}
void ShutdownImport() {
    if (importing) {
        TerminateJobObject(importing->job, ERROR_CANCELLED);
        WaitForSingleObject(importing->process, 5000);
        DeleteFileW(importing->output.c_str());
        importing.reset();
    }
}
} // namespace v0
