#pragma once
#include "settings.h"
namespace v0 {
struct Import {
    HANDLE process = nullptr, job = nullptr;
    std::wstring source, output, log, device;
    std::wstring progressFile;
    UINT64 started = GetTickCount64();
    bool cancelled = false;
    double durationSeconds = 0;
    int lastPercent = 0;
    ~Import() {
        if (job)
            CloseHandle(job);
        if (process)
            CloseHandle(process);
    }
};
extern std::unique_ptr<Import> importing;
void UpdateImportProgress();
void ImportVideo(const std::wstring& source, const std::wstring& device);
void PollImport();
void CancelImport();
void ShutdownImport();
} // namespace v0
