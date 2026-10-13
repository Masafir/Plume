#include "../app_state.h"
#include "bounded_decoder.h"

bool ModernHardwarePolicy() {
    using VersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    auto fn = reinterpret_cast<VersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW v{sizeof(v)};
    return fn && fn(&v) == 0 && v.dwBuildNumber >= 26200;
}

class Pipeline {
    std::shared_ptr<SharedGpu> shared;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VideoDevice> videoDevice;
    ComPtr<ID3D11VideoContext> videoContext;
    ComPtr<IMFDXGIDeviceManager> manager;
    ComPtr<IMFSourceReader> reader;
    std::unique_ptr<BoundedDecoder> bounded;
    ComPtr<IDXGISwapChain3> swap;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
    ComPtr<ID3D11VideoProcessor> processor;
    std::array<ComPtr<ID3D11VideoProcessorOutputView>, 1> outputs;
    struct Input {
        ComPtr<ID3D11Texture2D> texture;
        UINT slice = 0;
        ComPtr<ID3D11VideoProcessorInputView> view;
    };
    std::array<Input, 64> inputs;
    UINT inputCount = 0, width = 0, height = 0, videoWidth = 0, videoHeight = 0;
    UINT parN = 1, parD = 1, frameN = 30, frameD = 1, matrix = 1, range = 2;
    UINT interlaceMode = MFVideoInterlace_Progressive;
    UINT64 currentSize = 0;
    LONGLONG firstTimestamp = -1, clockStart = 0;
    LARGE_INTEGER frequency{};
    static LONGLONG Counter() {
        LARGE_INTEGER v{};
        QueryPerformanceCounter(&v);
        return v.QuadPart;
    }
    void Type() {
        ComPtr<IMFMediaType> type;
        if (bounded)
            type = bounded->Type();
        else
            Check(reader->GetCurrentMediaType(VideoStream, &type), L"Type vidéo");
        Check(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &videoWidth, &videoHeight),
              L"Dimensions vidéo");
        if (!videoWidth || !videoHeight)
            throw Failure{MF_E_INVALIDMEDIATYPE, L"Dimensions nulles"};
        if (FAILED(MFGetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, &parN, &parD)) || !parN || !parD)
            parN = parD = 1;
        if (FAILED(MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &frameN, &frameD)) || !frameN ||
            !frameD) {
            frameN = 30;
            frameD = 1;
        }
        matrix = MFGetAttributeUINT32(type.Get(), MF_MT_YUV_MATRIX,
                                      videoHeight >= 720 ? MFVideoTransferMatrix_BT709
                                                         : MFVideoTransferMatrix_BT601);
        range = MFGetAttributeUINT32(type.Get(), MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
        interlaceMode = MFGetAttributeUINT32(type.Get(), MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (interlaceMode != MFVideoInterlace_Progressive &&
            interlaceMode != MFVideoInterlace_MixedInterlaceOrProgressive)
            throw Failure{MF_E_INVALIDMEDIATYPE, L"Vidéo entrelacée : convertir en progressif"};
        UINT transfer = MFGetAttributeUINT32(type.Get(), MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709);
        if (transfer == MFVideoTransFunc_2084 || transfer == MFVideoTransFunc_HLG ||
            matrix == MFVideoTransferMatrix_BT2020_10 || matrix == MFVideoTransferMatrix_BT2020_12)
            throw Failure{MF_E_INVALIDMEDIATYPE, L"HDR/BT.2020 non pris en charge : convertir en SDR BT.709"};
    }
    void Processor() {
        if (state->root->auditNoRender)
            return;
        std::lock_guard guard(shared->renderMutex);
        inputs = {};
        inputCount = 0;
        for (auto& output : outputs)
            output.Reset();
        processor.Reset();
        enumerator.Reset();
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};
        desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        desc.InputFrameRate = {frameN, frameD};
        desc.OutputFrameRate = {frameN, frameD};
        desc.InputWidth = videoWidth;
        desc.InputHeight = videoHeight;
        desc.OutputWidth = width;
        desc.OutputHeight = height;
        desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        Check(videoDevice->CreateVideoProcessorEnumerator(&desc, &enumerator), L"VideoProcessorEnumerator");
        Check(videoDevice->CreateVideoProcessor(enumerator.Get(), 0, &processor), L"VideoProcessor");
        // D3D11 flip chains expose buffer zero; DXGI rotates its backing storage.
        // Indexing every buffer explicitly is a D3D12 pattern, not this D3D11 path.
        for (UINT i = 0; i < outputs.size(); ++i) {
            ComPtr<ID3D11Texture2D> back;
            Check(swap->GetBuffer(i, IID_PPV_ARGS(&back)), L"Backbuffer");
            D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC view{};
            view.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
            Check(
                videoDevice->CreateVideoProcessorOutputView(back.Get(), enumerator.Get(), &view, &outputs[i]),
                L"OutputView");
        }
        RECT src{0, 0, LONG(videoWidth), LONG(videoHeight)}, dst{0, 0, LONG(width), LONG(height)};
        double aspect = double(videoWidth) * parN / (double(videoHeight) * parD);
        double target = double(width) / height;
        if (state->options.fill) {
            if (aspect > target) {
                LONG cropped = LONG(std::round(videoHeight * target * parD / parN));
                src.left = (LONG(videoWidth) - cropped) / 2;
                src.right = src.left + cropped;
            } else {
                LONG cropped = LONG(std::round(videoWidth / target * parN / parD));
                src.top = (LONG(videoHeight) - cropped) / 2;
                src.bottom = src.top + cropped;
            }
        } else {
            if (aspect > target) {
                LONG h = LONG(std::round(width / aspect));
                dst.top = (LONG(height) - h) / 2;
                dst.bottom = dst.top + h;
            } else {
                LONG w = LONG(std::round(height * aspect));
                dst.left = (LONG(width) - w) / 2;
                dst.right = dst.left + w;
            }
        }
        D3D11_VIDEO_COLOR black{};
        black.RGBA.A = 1;
        videoContext->VideoProcessorSetOutputBackgroundColor(processor.Get(), FALSE, &black);
        videoContext->VideoProcessorSetStreamSourceRect(processor.Get(), 0, TRUE, &src);
        videoContext->VideoProcessorSetStreamDestRect(processor.Get(), 0, TRUE, &dst);
        videoContext->VideoProcessorSetStreamFrameFormat(processor.Get(), 0,
                                                         D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        videoContext->VideoProcessorSetStreamAutoProcessingMode(processor.Get(), 0, FALSE);
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE input{};
        input.YCbCr_Matrix = matrix == MFVideoTransferMatrix_BT709 ? 1 : 0;
        input.Nominal_Range = range == MFNominalRange_0_255 ? 1 : 2;
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE output{};
        output.RGB_Range = 0;
        videoContext->VideoProcessorSetStreamColorSpace(processor.Get(), 0, &input);
        videoContext->VideoProcessorSetOutputColorSpace(processor.Get(), &output);
    }
    void Resize() {
        if (state->root->auditNoRender)
            return;
        UINT64 requested = state->size.load();
        if (requested == currentSize || !UINT(requested) || !UINT(requested >> 32))
            return;
        currentSize = requested;
        width = UINT(requested);
        height = UINT(requested >> 32);
        std::lock_guard guard(shared->renderMutex);
        for (auto& output : outputs)
            output.Reset();
        context->ClearState();
        context->Flush();
        Check(swap->ResizeBuffers(2, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0), L"ResizeBuffers");
        Processor();
    }
    void Preview(const D3D11_VIDEO_PROCESSOR_STREAM& stream) {
        if constexpr (!V0Build)
            return;
        std::lock_guard lock(state->previewMutex);
        auto& p = state->preview;
        if (!p.window || p.failed || !IsWindowVisible(p.window) || IsIconic(GetAncestor(p.window, GA_ROOT)) ||
            GetTickCount64() < p.nextFrame)
            return;
        p.nextFrame = GetTickCount64() + 66; // At most 15 fps; reuse this decoded sample.
        try {
            std::lock_guard gpuLock(shared->renderMutex);
            RECT client{};
            GetClientRect(p.window, &client);
            UINT w = UINT(client.right), h = UINT(client.bottom);
            if (!w || !h)
                return;
            if (w != p.width || h != p.height) {
                p.output.Reset();
                p.swap.Reset();
                p.enumerator.Reset();
                context->Flush();
            }
            if (!p.swap) {
                ComPtr<IDXGIDevice> dxgi;
                ComPtr<IDXGIAdapter> adapter;
                ComPtr<IDXGIFactory2> factory;
                Check(device.As(&dxgi), L"Aperçu DXGI");
                Check(dxgi->GetAdapter(&adapter), L"Aperçu adaptateur");
                Check(adapter->GetParent(IID_PPV_ARGS(&factory)), L"Aperçu factory");
                DXGI_SWAP_CHAIN_DESC1 desc{};
                desc.Width = w;
                desc.Height = h;
                desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                desc.BufferCount = 2;
                desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
                Check(
                    factory->CreateSwapChainForHwnd(device.Get(), p.window, &desc, nullptr, nullptr, &p.swap),
                    L"Aperçu swapchain");
                p.width = w;
                p.height = h;
            }
            if (p.enumerator.Get() != enumerator.Get()) {
                p.output.Reset();
                ComPtr<ID3D11Texture2D> back;
                Check(p.swap->GetBuffer(0, IID_PPV_ARGS(&back)), L"Aperçu backbuffer");
                D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC view{};
                view.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
                Check(videoDevice->CreateVideoProcessorOutputView(back.Get(), enumerator.Get(), &view,
                                                                  &p.output),
                      L"Aperçu output view");
                p.enumerator = enumerator;
            }
            BOOL destEnabled = FALSE, targetEnabled = FALSE;
            RECT dest{}, target{};
            videoContext->VideoProcessorGetStreamDestRect(processor.Get(), 0, &destEnabled, &dest);
            videoContext->VideoProcessorGetOutputTargetRect(processor.Get(), &targetEnabled, &target);
            double scale = std::min(double(w) / width, double(h) / height);
            LONG x = LONG((w - width * scale) / 2), y = LONG((h - height * scale) / 2);
            RECT previewRect{x + LONG(dest.left * scale), y + LONG(dest.top * scale),
                             x + LONG(dest.right * scale), y + LONG(dest.bottom * scale)},
                all{0, 0, LONG(w), LONG(h)};
            videoContext->VideoProcessorSetStreamDestRect(processor.Get(), 0, TRUE, &previewRect);
            videoContext->VideoProcessorSetOutputTargetRect(processor.Get(), TRUE, &all);
            HRESULT rendered =
                videoContext->VideoProcessorBlt(processor.Get(), p.output.Get(), 0, 1, &stream);
            videoContext->VideoProcessorSetStreamDestRect(processor.Get(), 0, destEnabled, &dest);
            videoContext->VideoProcessorSetOutputTargetRect(processor.Get(), targetEnabled, &target);
            Check(rendered, L"Aperçu rendu GPU");
            HRESULT presented = p.swap->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
            if (presented == DXGI_ERROR_WAS_STILL_DRAWING)
                return;
            Check(presented, L"Aperçu présentation");
            if (++state->previewFrames == 1)
                Log(L"Première image aperçu GPU présentée (sans second décodeur)");
        } catch (Failure f) {
            p.output.Reset();
            p.swap.Reset();
            p.enumerator.Reset();
            p.failed = true;
            wchar_t text[256]{};
            swprintf_s(text, L"Aperçu désactivé : %s 0x%08X", f.operation, unsigned(f.hr));
            Log(text);
        }
    }
    ID3D11VideoProcessorInputView* InputView(ID3D11Texture2D* texture, UINT slice) {
        for (UINT i = 0; i < inputCount; ++i)
            if (inputs[i].texture.Get() == texture && inputs[i].slice == slice)
                return inputs[i].view.Get();
        // Fixed bound: no vector growth or unbounded retention of decoder surfaces.
        if (inputCount == inputs.size())
            throw Failure{E_OUTOFMEMORY, L"Pool décodeur > 64 surfaces : arrêt pour préserver la RAM"};
        Input& entry = inputs[inputCount];
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC view{};
        view.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        view.Texture2D.ArraySlice = slice;
        Check(videoDevice->CreateVideoProcessorInputView(texture, enumerator.Get(), &view, &entry.view),
              L"InputView");
        entry.texture = texture;
        entry.slice = slice;
        ++inputCount;
        return entry.view.Get();
    }

  public:
    Pipeline() {
        QueryPerformanceFrequency(&frequency);
        Audit(L"before-D3D11");
        {
            std::lock_guard guard(state->root->gpuMutex);
            if (!state->root->gpu)
                state->root->gpu = std::make_shared<SharedGpu>();
            shared = state->root->gpu;
        }
        device = shared->device;
        context = shared->context;
        Audit(L"after-D3D11", true);
        {
            ComPtr<IDXGIDevice> dxgi;
            ComPtr<IDXGIAdapter> adapter;
            if (SUCCEEDED(device.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter))) {
                DXGI_ADAPTER_DESC desc{};
                adapter->GetDesc(&desc);
                Log(desc.Description);
            }
        }
        Check(device.As(&videoDevice), L"ID3D11VideoDevice");
        Check(context.As(&videoContext), L"ID3D11VideoContext");
        UINT token = 0;
        Check(MFCreateDXGIDeviceManager(&token, &manager), L"DXGI device manager");
        Check(manager->ResetDevice(device.Get(), token), L"ResetDevice");
        if (state->root->bounded) {
            bounded = std::make_unique<BoundedDecoder>(
                state->options.video, manager.Get(),
                state->root->decoderSamples ? state->root->decoderSamples : 1, state->root->boundedLowLatency,
                Log);
            Type();
            wchar_t text[192]{};
            swprintf_s(text,
                       L"Bounded MP4 + direct MFT: packets=%zu, compressed buffers=%u, max=%u bytes total=%u "
                       L"bytes; GPU surfaces required",
                       bounded->PacketCount(), bounded->CompressedBuffers(), bounded->PacketCapacity(),
                       bounded->CompressedBytes());
            Log(text);
            Audit(L"after-bounded-decoder", true);
        } else {
            ComPtr<IMFAttributes> attributes;
            Check(MFCreateAttributes(&attributes, 6), L"MF attributes");
            Check(attributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, manager.Get()), L"MF D3D manager");
            Check(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE),
                  L"MF hardware transforms");
            Check(attributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, FALSE), L"MF DXVA");
            Check(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, FALSE), L"MF processing");
            if (!state->options.compat) {
                if (!ModernHardwarePolicy())
                    throw Failure{
                        HRESULT_FROM_WIN32(ERROR_OLD_WIN_VERSION),
                        L"Mode matériel strict : Windows 11 25H2 requis ; --compat pour DXVA non garanti"};
                Check(attributes->SetUINT32(MF_READWRITE_USE_ONLY_HARDWARE_TRANSFORMS, TRUE),
                      L"MF strict hardware policy");
            }
            Check(MFCreateSourceReaderFromURL(state->options.video.c_str(), attributes.Get(), &reader),
                  L"Ouvrir la vidéo / codec matériel");
            Audit(L"after-source-open");
            Check(reader->SetStreamSelection(AllStreams, FALSE), L"Désactiver audio");
            Check(reader->SetStreamSelection(VideoStream, TRUE), L"Sélection vidéo");
            {
                ComPtr<IMFMediaType> native;
                GUID codec{};
                if (SUCCEEDED(reader->GetNativeMediaType(VideoStream, 0, &native)) &&
                    SUCCEEDED(native->GetGUID(MF_MT_SUBTYPE, &codec))) {
                    wchar_t guid[64]{};
                    StringFromGUID2(codec, guid, 64);
                    Log(guid);
                }
            }
            HRESULT selected = MF_E_INVALIDMEDIATYPE;
            for (const GUID subtype : {MFVideoFormat_NV12, MFVideoFormat_P010}) {
                ComPtr<IMFMediaType> type;
                Check(MFCreateMediaType(&type), L"Créer type vidéo");
                Check(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), L"Major type");
                Check(type->SetGUID(MF_MT_SUBTYPE, subtype), L"Subtype");
                selected = reader->SetCurrentMediaType(VideoStream, nullptr, type.Get());
                if (SUCCEEDED(selected))
                    break;
            }
            Check(selected, L"Décodage NV12/P010 matériel non disponible");
            Type();
            if (state->root->auditMemory || state->root->decoderSamples) {
                ComPtr<IMFSourceReaderEx> extended;
                Check(reader.As(&extended), L"Inspection SourceReaderEx");
                ComPtr<IMFTransform> decoder;
                GUID category{};
                Check(extended->GetTransformForStream(VideoStream, 0, &category, &decoder),
                      L"Inspection du décodeur MFT");
                ComPtr<IMFAttributes> decoderAttributes;
                Check(decoder->GetAttributes(&decoderAttributes), L"Attributs du décodeur");
                UINT32 original = 0;
                HRESULT found =
                    decoderAttributes->GetUINT32(MF_SA_MINIMUM_OUTPUT_SAMPLE_COUNT_PROGRESSIVE, &original);
                wchar_t text[192]{};
                swprintf_s(text, L"MFT progressive output minimum: HRESULT=0x%08X value=%u", unsigned(found),
                           original);
                Log(text);
                if (state->root->decoderSamples) {
                    Check(decoderAttributes->SetUINT32(MF_SA_MINIMUM_OUTPUT_SAMPLE_COUNT_PROGRESSIVE,
                                                       state->root->decoderSamples),
                          L"Minimum de sortie progressif");
                    swprintf_s(text,
                               L"EXPERIMENT: progressive output minimum requested=%u (actual texture pool "
                               L"checked on first frame)",
                               state->root->decoderSamples);
                    Log(text);
                }
            }
            Audit(L"after-decoder-negotiation", true);
            Log(state->options.compat ? L"SourceReader prêt : compat, matériel non garanti"
                                      : L"SourceReader prêt : politique matériel stricte");
        }
        if (state->root->auditNoRender) {
            Log(L"AUDIT : décodage seul, aucun swapchain / VideoProcessor / Present");
            return;
        }
        ComPtr<IDXGIDevice> dxgiDevice;
        Check(device.As(&dxgiDevice), L"DXGI device");
        ComPtr<IDXGIAdapter> adapter;
        Check(dxgiDevice->GetAdapter(&adapter), L"DXGI adapter");
        ComPtr<IDXGIFactory2> factory;
        Check(adapter->GetParent(IID_PPV_ARGS(&factory)), L"DXGI factory");
        currentSize = state->size.load();
        width = std::max(1u, UINT(currentSize));
        height = std::max(1u, UINT(currentSize >> 32));
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        ComPtr<IDXGISwapChain1> chain;
        Check(factory->CreateSwapChainForHwnd(device.Get(), state->window, &desc, nullptr, nullptr, &chain),
              L"Swapchain HWND");
        Check(chain.As(&swap), L"Swapchain3");
        factory->MakeWindowAssociation(state->window, DXGI_MWA_NO_ALT_ENTER);
        Processor();
        Log(L"Swapchain et VideoProcessor prêts");
        Audit(L"after-swapchain-processor", true);
    }
    void Run() {
        bool cycleHasSample = false;
        while (!state->stop) {
            if (state->pause) {
                // Hold at most one sample (here none); no decode or Present while paused.
                WaitForSingleObject(state->wake.value, INFINITE);
                firstTimestamp = -1;
                continue;
            }
            Resize();
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            if (bounded)
                bounded->Read(flags, timestamp, sample);
            else
                Check(reader->ReadSample(VideoStream, 0, nullptr, &flags, &timestamp, &sample),
                      L"ReadSample");
            if (flags & MF_SOURCE_READERF_ERROR)
                throw Failure{E_FAIL, L"Erreur SourceReader"};
            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
                Type();
                Processor();
                firstTimestamp = -1;
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                if (!cycleHasSample)
                    throw Failure{MF_E_INVALIDMEDIATYPE, L"Vidéo sans frames décodables"};
                PROPVARIANT position{};
                position.vt = VT_I8;
                position.hVal.QuadPart = 0;
                if (bounded)
                    bounded->Rewind();
                else {
                    if (state->root->flushLoop)
                        Check(reader->Flush(AllStreams), L"Purge SourceReader au bouclage");
                    Check(reader->SetCurrentPosition(GUID_NULL, position), L"Reboucler (vidéo non seekable)");
                }
                wchar_t text[128]{};
                swprintf_s(text, L"Loop : cacheInputViews=%u frames=%llu", inputCount, state->frames.load());
                Log(text);
                Audit(L"after-loop-seek");
                firstTimestamp = -1;
                cycleHasSample = false;
                continue;
            }
            if (!sample) {
                WaitForSingleObject(state->wake.value, 10);
                continue;
            }
            cycleHasSample = true;
            ++state->samples;
            UINT interlaced = FALSE;
            HRESULT interlaceAttribute = sample->GetUINT32(MFSampleExtension_Interlaced, &interlaced);
            if (interlaced ||
                (interlaceMode == MFVideoInterlace_MixedInterlaceOrProgressive && FAILED(interlaceAttribute)))
                throw Failure{MF_E_INVALIDMEDIATYPE, L"Frame entrelacée ou indicateur progressif absent"};
            ComPtr<IMFMediaBuffer> buffer;
            Check(sample->GetBufferByIndex(0, &buffer), L"Sample buffer");
            ComPtr<IMFDXGIBuffer> dxgi;
            Check(buffer.As(&dxgi), L"Sample CPU refusé : IMFDXGIBuffer requis");
            ComPtr<ID3D11Texture2D> texture;
            Check(dxgi->GetResource(IID_PPV_ARGS(&texture)), L"Texture GPU");
            UINT slice = 0;
            Check(dxgi->GetSubresourceIndex(&slice), L"Subresource");
            D3D11_TEXTURE2D_DESC td{};
            texture->GetDesc(&td);
            if (state->frames == 0) {
                wchar_t text[256]{};
                swprintf_s(text, L"Texture : %u x %u, arraySize=%u format=%u, bind=0x%X, CPUAccess=0x%X",
                           td.Width, td.Height, td.ArraySize, unsigned(td.Format), td.BindFlags,
                           td.CPUAccessFlags);
                Log(text);
            }
            if (td.Usage != D3D11_USAGE_DEFAULT || td.CPUAccessFlags || td.MipLevels != 1)
                throw Failure{E_FAIL, L"Surface CPU/staging refusée"};
            if (state->frames == 0)
                Audit(L"after-first-decode", true);
            if (firstTimestamp < 0) {
                firstTimestamp = timestamp;
                clockStart = Counter();
            }
            LONGLONG due =
                clockStart + LONGLONG((timestamp - firstTimestamp) * double(frequency.QuadPart) / 10000000.0);
            while (!state->stop && !state->pause) {
                LONGLONG remaining = due - Counter();
                if (remaining <= 0)
                    break;
                DWORD ms = DWORD(std::clamp(remaining * 1000 / frequency.QuadPart, 1LL, 1000LL));
                WaitForSingleObject(state->wake.value, ms);
            }
            if (state->stop)
                break;
            if (state->pause) {
                firstTimestamp = -1;
                continue;
            }
            if (state->root->auditNoRender) {
                ++state->frames;
                continue;
            }
            Resize();
            {
                std::lock_guard guard(shared->renderMutex);
                D3D11_VIDEO_PROCESSOR_STREAM stream{};
                stream.Enable = TRUE;
                stream.pInputSurface = InputView(texture.Get(), slice);
                Check(videoContext->VideoProcessorBlt(processor.Get(), outputs[0].Get(), 0, 1, &stream),
                      L"VideoProcessorBlt");
            }
            {
                D3D11_VIDEO_PROCESSOR_STREAM stream{};
                stream.Enable = TRUE;
                stream.pInputSurface = InputView(texture.Get(), slice);
                Preview(stream);
            }
            Check(swap->Present(1, 0), L"Present / GPU retiré");
            if (++state->frames == 1) {
                Log(L"Première frame GPU présentée");
                Audit(L"after-first-present", true);
            }
        }
    }
};

void Playback() {
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HRESULT mf = E_FAIL;
    try {
        Check(com, L"COM playback");
        mf = MFStartup(MF_VERSION, MFSTARTUP_FULL);
        Check(mf, L"MFStartup");
        Audit(L"after-MFStartup");
        unsigned retries = 0;
        while (!state->stop) {
            try {
                Pipeline pipeline;
                pipeline.Run();
                break;
            } catch (Failure f) {
                bool removed = f.hr == DXGI_ERROR_DEVICE_REMOVED || f.hr == DXGI_ERROR_DEVICE_RESET ||
                               f.hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
                if (!removed || ++retries > 3)
                    throw;
                {
                    std::lock_guard guard(state->root->gpuMutex);
                    state->root->gpu.reset();
                }
                WaitForSingleObject(state->wake.value, 1000);
            }
        }
    } catch (Failure f) {
        state->error = f;
        wchar_t text[512]{};
        swprintf_s(text, L"ÉCHEC : %s (0x%08X)", f.operation, unsigned(f.hr));
        Log(text);
        PostMessageW(state->window, ErrorMessage, 0, 0);
    } catch (...) {
        state->error = {E_UNEXPECTED, L"Erreur inattendue"};
        PostMessageW(state->window, ErrorMessage, 0, 0);
    }
    if (SUCCEEDED(mf))
        MFShutdown();
    if (SUCCEEDED(com))
        CoUninitialize();
}
