#include "../app_state.h"

SharedGpu::SharedGpu() {
    D3D_FEATURE_LEVEL level{};
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                            D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                            D3D11_SDK_VERSION, &device, &level, &context),
          L"D3D11 GPU (aucun WARP)");
    ComPtr<ID3D10Multithread> multithread;
    Check(context.As(&multithread), L"D3D multithread");
    multithread->SetMultithreadProtected(TRUE);
}
