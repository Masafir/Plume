#include "app_state.h"
#include "memory_audit.h"

void Log(const wchar_t* text) {
    State* root = state->root;
    if (!root->report)
        return;
    std::lock_guard guard(root->logMutex);
    fwprintf(root->report, L"[%llu] %s %s\n", GetTickCount64(), state->monitorDevice.c_str(), text);
    fflush(root->report);
}
void Audit(const wchar_t* stage, bool detail) {
    if (!state->root->auditMemory || !state->root->report)
        return;
    MemoryAudit(stage, detail, [](const wchar_t* text) { Log(text); });
    std::shared_ptr<SharedGpu> gpu;
    {
        std::lock_guard guard(state->root->gpuMutex);
        gpu = state->root->gpu;
    }
    if (!gpu)
        return;
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIAdapter3> adapter3;
    if (SUCCEEDED(gpu->device.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter)) &&
        SUCCEEDED(adapter.As(&adapter3))) {
        DXGI_QUERY_VIDEO_MEMORY_INFO local{}, nonlocal{};
        if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)) &&
            SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonlocal))) {
            wchar_t text[256]{};
            swprintf_s(text, L"MEM GPU per-process localUsage=%.2f MiB nonLocalUsage=%.2f MiB",
                       double(local.CurrentUsage) / 1048576, double(nonlocal.CurrentUsage) / 1048576);
            Log(text);
        }
    }
}
