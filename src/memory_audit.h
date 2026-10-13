#pragma once
#include <windows.h>
#include <psapi.h>
#include <array>
#include <vector>
#include <algorithm>
#include <cstdio>

// Diagnostic only. Not used unless --audit-memory is explicitly selected.
// Snapshot counters are read before allocating the working-set query buffer.
template <class Emit> void MemoryAudit(const wchar_t* stage, bool detailed, Emit emit) {
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                              sizeof(memory)))
        return;
    wchar_t text[1024]{};
    swprintf_s(text, L"MEM stage=%s WS=%.2f MiB privateCommit=%.2f MiB peakWS=%.2f MiB", stage,
               double(memory.WorkingSetSize) / 1048576, double(memory.PrivateUsage) / 1048576,
               double(memory.PeakWorkingSetSize) / 1048576);
    emit(text);
    PROCESS_MEMORY_COUNTERS_EX2 extended{};
    extended.cb = sizeof(extended);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&extended),
                             sizeof(extended))) {
        swprintf_s(text, L"MEM extended privateWS=%.2f MiB sharedCommit=%.2f MiB",
                   double(extended.PrivateWorkingSetSize) / 1048576,
                   double(extended.SharedCommitUsage) / 1048576);
        emit(text);
    }
    if (!detailed)
        return;
    struct Module {
        ULONG_PTR start = 0, end = 0;
        SIZE_T resident = 0, nonShared = 0;
        wchar_t path[MAX_PATH]{};
    };
    std::array<HMODULE, 512> handles{};
    DWORD needed = 0;
    std::vector<Module> modules;
    if (EnumProcessModules(GetCurrentProcess(), handles.data(), sizeof(handles), &needed)) {
        modules.reserve(std::min<size_t>(needed / sizeof(HMODULE), handles.size()));
        for (size_t i = 0; i < std::min<size_t>(needed / sizeof(HMODULE), handles.size()); ++i) {
            MODULEINFO info{};
            if (!GetModuleInformation(GetCurrentProcess(), handles[i], &info, sizeof(info)))
                continue;
            Module m;
            m.start = reinterpret_cast<ULONG_PTR>(info.lpBaseOfDll);
            m.end = m.start + info.SizeOfImage;
            GetModuleFileNameW(handles[i], m.path, MAX_PATH);
            modules.push_back(m);
        }
    }
    std::sort(modules.begin(), modules.end(),
              [](const Module& a, const Module& b) { return a.start < b.start; });
    std::vector<BYTE> buffer(1024 * 1024);
    bool success = false;
    for (unsigned attempt = 0; attempt < 6; ++attempt) {
        if (QueryWorkingSet(GetCurrentProcess(), buffer.data(), DWORD(buffer.size()))) {
            success = true;
            break;
        }
        if (GetLastError() != ERROR_BAD_LENGTH)
            break;
        buffer.resize(buffer.size() * 2);
    }
    if (!success) {
        emit(L"MEM working-set detail unavailable");
        return;
    }
    auto* ws = reinterpret_cast<PSAPI_WORKING_SET_INFORMATION*>(buffer.data());
    const SIZE_T maxEntries = (buffer.size() - offsetof(PSAPI_WORKING_SET_INFORMATION, WorkingSetInfo)) /
                              sizeof(PSAPI_WORKING_SET_BLOCK);
    if (ws->NumberOfEntries > maxEntries)
        return;
    std::sort(ws->WorkingSetInfo, ws->WorkingSetInfo + ws->NumberOfEntries,
              [](auto a, auto b) { return a.VirtualPage < b.VirtualPage; });
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    SIZE_T image = 0, privateRegions = 0, mapped = 0, other = 0, nonShared = 0, imagesNonShared = 0;
    MEMORY_BASIC_INFORMATION region{};
    ULONG_PTR regionEnd = 0;
    size_t moduleIndex = 0;
    for (ULONG_PTR i = 0; i < ws->NumberOfEntries; ++i) {
        auto entry = ws->WorkingSetInfo[i];
        ULONG_PTR address = entry.VirtualPage * system.dwPageSize;
        if (address < reinterpret_cast<ULONG_PTR>(region.BaseAddress) || address >= regionEnd) {
            if (!VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region))) {
                other += system.dwPageSize;
                continue;
            }
            regionEnd = reinterpret_cast<ULONG_PTR>(region.BaseAddress) + region.RegionSize;
        }
        if (!entry.Shared)
            nonShared += system.dwPageSize;
        if (region.Type == MEM_IMAGE) {
            image += system.dwPageSize;
            if (!entry.Shared)
                imagesNonShared += system.dwPageSize;
        } else if (region.Type == MEM_PRIVATE)
            privateRegions += system.dwPageSize;
        else if (region.Type == MEM_MAPPED)
            mapped += system.dwPageSize;
        else
            other += system.dwPageSize;
        while (moduleIndex < modules.size() && modules[moduleIndex].end <= address)
            ++moduleIndex;
        if (moduleIndex < modules.size() && modules[moduleIndex].start <= address &&
            address < modules[moduleIndex].end) {
            modules[moduleIndex].resident += system.dwPageSize;
            if (!entry.Shared)
                modules[moduleIndex].nonShared += system.dwPageSize;
        }
    }
    swprintf_s(text,
               L"MEM residentDetail images=%.2f MiB privateRegions=%.2f MiB mapped=%.2f MiB other=%.2f MiB "
               L"allNonShareable=%.2f MiB imageNonShareable=%.2f MiB probeBuffer=%.2f MiB",
               double(image) / 1048576, double(privateRegions) / 1048576, double(mapped) / 1048576,
               double(other) / 1048576, double(nonShared) / 1048576, double(imagesNonShared) / 1048576,
               double(buffer.size()) / 1048576);
    emit(text);
    std::sort(modules.begin(), modules.end(),
              [](const Module& a, const Module& b) { return a.resident > b.resident; });
    for (size_t i = 0; i < std::min<size_t>(15, modules.size()); ++i) {
        const auto& m = modules[i];
        const wchar_t* name = wcsrchr(m.path, L'\\');
        name = name ? name + 1 : m.path;
        swprintf_s(text, L"MEM module=%s resident=%.2f MiB nonShareable=%.2f MiB virtualImage=%.2f MiB", name,
                   double(m.resident) / 1048576, double(m.nonShared) / 1048576,
                   double(m.end - m.start) / 1048576);
        emit(text);
    }
}
