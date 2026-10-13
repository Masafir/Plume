#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <cstdio>
#include <initializer_list>
using Microsoft::WRL::ComPtr;
int main() {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    HRESULT hr =
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                          nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
    if (FAILED(hr)) {
        printf("D3D11CreateDevice HRESULT=0x%08X\n", unsigned(hr));
        return 1;
    }
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    if (SUCCEEDED(device.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter))) {
        DXGI_ADAPTER_DESC desc{};
        adapter->GetDesc(&desc);
        wprintf(L"Adapter: %s\n", desc.Description);
    }
    ComPtr<ID3D11VideoDevice> video;
    hr = device.As(&video);
    if (FAILED(hr))
        return 1;
    BOOL supported = FALSE;
    hr = video->CheckVideoDecoderFormat(&D3D11_DECODER_PROFILE_H264_VLD_NOFGT, DXGI_FORMAT_NV12, &supported);
    printf("H264 VLD NOFGT / NV12 support=%d HRESULT=0x%08X\n", supported, unsigned(hr));
    for (const auto size : {SIZE{1920, 1080}, SIZE{3840, 2160}}) {
        D3D11_VIDEO_DECODER_DESC desc{};
        desc.Guid = D3D11_DECODER_PROFILE_H264_VLD_NOFGT;
        desc.SampleWidth = UINT(size.cx);
        desc.SampleHeight = UINT(size.cy);
        desc.OutputFormat = DXGI_FORMAT_NV12;
        UINT count = 0;
        hr = video->GetVideoDecoderConfigCount(&desc, &count);
        printf("Size=%ldx%ld configs=%u HRESULT=0x%08X\n", size.cx, size.cy, count, unsigned(hr));
        for (UINT i = 0; i < count; ++i) {
            D3D11_VIDEO_DECODER_CONFIG config{};
            hr = video->GetVideoDecoderConfig(&desc, i, &config);
            if (FAILED(hr))
                return 1;
            printf("  Config=%u BitstreamRaw=%u MinRenderTargetBuffCount=%u DecoderSpecific=0x%X\n", i,
                   config.ConfigBitstreamRaw, unsigned(config.ConfigMinRenderTargetBuffCount),
                   unsigned(config.ConfigDecoderSpecific));
        }
    }
    return 0;
}
