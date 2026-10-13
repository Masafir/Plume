#pragma once
#include "bounded_mp4.h"
#include <wmcodecdsp.h>
#include <strmif.h>
#include <codecapi.h>

// Directly drives the synchronous H.264 MFT, not SourceReader. The MFT
// still owns decoder surfaces: this is the bounded-demux intermediate backend.
class BoundedDecoder {
    BoundedMp4 source;
    ComPtr<IMFTransform> transform;
    ComPtr<IMFMediaType> outputType;
    bool draining = false;
    unsigned inputs = 0, outputs = 0;
    void (*logger)(const wchar_t*) = nullptr;
    void SelectOutput() {
        for (DWORD index = 0;; ++index) {
            ComPtr<IMFMediaType> type;
            HRESULT hr = transform->GetOutputAvailableType(0, index, &type);
            if (hr == MF_E_NO_MORE_TYPES)
                break;
            Check(hr, L"Types de sortie MFT direct");
            GUID subtype{};
            if (SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) && subtype == MFVideoFormat_NV12) {
                Check(transform->SetOutputType(0, type.Get(), 0), L"Sortie NV12 MFT direct");
                outputType = type;
                return;
            }
        }
        throw Failure{MF_E_INVALIDMEDIATYPE, L"MFT direct : sortie NV12 indisponible"};
    }

  public:
    BoundedDecoder(const std::wstring& path, IMFDXGIDeviceManager* manager, UINT minimum, bool lowLatency,
                   void (*log)(const wchar_t*))
        : source(path, lowLatency ? 4 : 16), logger(log) {
        Check(CoCreateInstance(CLSID_CMSH264DecoderMFT, nullptr, CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(&transform)),
              L"Créer MFT H264 direct");
        ComPtr<IMFAttributes> attributes;
        Check(transform->GetAttributes(&attributes), L"Attributs MFT direct");
        if (!MFGetAttributeUINT32(attributes.Get(), MF_SA_D3D11_AWARE, FALSE))
            throw Failure{MF_E_UNSUPPORTED_D3D_TYPE, L"MFT direct sans D3D11"};
        Check(attributes->SetUINT32(MF_SA_MINIMUM_OUTPUT_SAMPLE_COUNT, minimum),
              L"Minimum de sortie MFT direct");
        Check(attributes->SetUINT32(MF_SA_MINIMUM_OUTPUT_SAMPLE_COUNT_PROGRESSIVE, minimum),
              L"Minimum progressif MFT direct");
        if (lowLatency) {
            ComPtr<ICodecAPI> codec;
            Check(transform.As(&codec), L"CodecAPI faible latence");
            VARIANT value{};
            value.vt = VT_UI4;
            value.ulVal = 1;
            Check(codec->SetValue(&CODECAPI_AVLowLatencyMode, &value), L"Activer faible latence H264");
        }
        Check(transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(manager)),
              L"Gestionnaire D3D11 MFT direct");
        auto input = source.Type();
        Check(transform->SetInputType(0, input.Get(), 0), L"Entrée Annex B MFT direct");
        SelectOutput();
        Check(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0),
              L"Begin streaming MFT direct");
        Check(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0), L"Start stream MFT direct");
    }
    ~BoundedDecoder() {
        if (transform) {
            transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
            transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        }
    }
    IMFMediaType* Type() const {
        return outputType.Get();
    }
    UINT PacketCapacity() const {
        return source.capacity;
    }
    UINT CompressedBytes() const {
        return source.allocatedBytes;
    }
    UINT CompressedBuffers() const {
        return source.BufferCount();
    }
    size_t PacketCount() const {
        return source.Count();
    }
    void Rewind() {
        if (logger) {
            wchar_t text[128]{};
            swprintf_s(text, L"Direct MFT completed cycle: inputs=%u outputs=%u", inputs, outputs);
            logger(text);
        }
        Check(transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0), L"Purge MFT direct");
        source.Rewind();
        draining = false;
        inputs = outputs = 0;
        Check(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0), L"Boucle MFT direct");
    }
    void Read(DWORD& flags, LONGLONG& timestamp, ComPtr<IMFSample>& sample) {
        flags = 0;
        sample.Reset();
        // Bound the amount of compressed input consumed before one output.
        for (unsigned attempts = 0; attempts < 32; ++attempts) {
            MFT_OUTPUT_STREAM_INFO info{};
            Check(transform->GetOutputStreamInfo(0, &info), L"Output info MFT direct");
            if (!(info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES))
                throw Failure{MF_E_UNSUPPORTED_D3D_TYPE, L"MFT direct ne fournit pas ses surfaces GPU"};
            MFT_OUTPUT_DATA_BUFFER output{};
            DWORD status = 0;
            HRESULT hr = transform->ProcessOutput(0, 1, &output, &status);
            if (logger && inputs <= 16 && outputs == 0) {
                wchar_t text[160]{};
                swprintf_s(text, L"Direct MFT startup: input=%u outputHRESULT=0x%08X outputFlags=0x%X",
                           inputs, unsigned(hr), output.dwStatus);
                logger(text);
            }
            ComPtr<IMFCollection> events;
            events.Attach(output.pEvents);
            ComPtr<IMFSample> produced;
            produced.Attach(output.pSample);
            if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
                SelectOutput();
                flags |= MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED;
                continue;
            }
            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
                if (draining) {
                    flags |= MF_SOURCE_READERF_ENDOFSTREAM;
                    return;
                }
                auto packet = source.Next();
                if (!packet) {
                    Check(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0),
                          L"Fin entrée MFT direct");
                    Check(transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0), L"Drain MFT direct");
                    draining = true;
                    continue;
                }
                Check(transform->ProcessInput(0, packet.Get(), 0), L"Paquet compressé MFT direct");
                ++inputs;
                continue;
            }
            Check(hr, L"Décodage MFT direct");
            if (!produced)
                continue;
            Check(produced->GetSampleTime(&timestamp), L"Timestamp MFT direct");
            ++outputs;
            sample = produced;
            return;
        }
        throw Failure{MF_E_NOTACCEPTING, L"MFT direct : trop de paquets sans sortie"};
    }
};
