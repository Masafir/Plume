#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <vector>
#include <memory>
#include <string>
#include <algorithm>
#include <cstdio>
using Microsoft::WRL::ComPtr;
#include "../src/video/bounded_mp4.h"
static void Expect(bool value) {
    if (!value)
        throw Failure{E_FAIL, L"Assertion de test"};
}
static std::array<BYTE, 32> Prefix(IMFSample* sample) {
    ComPtr<IMFMediaBuffer> buffer;
    Check(sample->GetBufferByIndex(0, &buffer), L"Buffer test");
    BYTE* data = nullptr;
    DWORD length = 0;
    Check(buffer->Lock(&data, nullptr, &length), L"Lock compressé test");
    std::array<BYTE, 32> prefix{};
    Expect(length >= prefix.size());
    memcpy(prefix.data(), data, prefix.size());
    Check(buffer->Unlock(), L"Unlock test");
    return prefix;
}
static void RejectFixture(const std::vector<BYTE>& contents) {
    wchar_t directory[MAX_PATH]{}, path[MAX_PATH]{};
    Expect(GetTempPathW(MAX_PATH, directory) != 0);
    Expect(GetTempFileNameW(directory, L"pwm", 0, path) != 0);
    struct Cleanup {
        const wchar_t* path;
        ~Cleanup() {
            DeleteFileW(path);
        }
    } cleanup{path};
    HANDLE file =
        CreateFileW(path, GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Expect(file != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    BOOL ok = WriteFile(file, contents.data(), DWORD(contents.size()), &written, nullptr);
    CloseHandle(file);
    Expect(ok && written == contents.size());
    bool rejected = false;
    try {
        BoundedMp4 invalid(path);
    } catch (const Failure& failure) {
        rejected = failure.hr == MF_E_INVALIDMEDIATYPE;
    }
    Expect(rejected);
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        fwprintf(stderr, L"Usage: BoundedMp4Tests prepared.mp4 reordered.mp4\n");
        return 2;
    }
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HRESULT mf = E_FAIL;
    int result = 0;
    try {
        Check(com, L"COM test");
        mf = MFStartup(MF_VERSION);
        Check(mf, L"MF test");
        BoundedMp4 source(argv[1]);
        Expect(source.width == 3840 && source.height == 2160 && source.Count() == 1229);
        std::array<ComPtr<IMFSample>, 16> held;
        for (auto& sample : held) {
            sample = source.Next();
            Expect(sample != nullptr);
        }
        auto first = Prefix(held[0].Get());
        Expect(first[0] == 0 && first[1] == 0 && first[2] == 0 && first[3] == 1 && (first[4] & 31) == 7);
        bool exhausted = false;
        try {
            auto extra = source.Next();
        } catch (const Failure& failure) {
            exhausted = failure.hr == MF_E_NOTACCEPTING;
        }
        Expect(exhausted && Prefix(held[0].Get()) == first);
        held[0].Reset();
        auto next = source.Next();
        Expect(next != nullptr);
        next.Reset();
        held = {};
        source.Rewind();
        size_t count = 0;
        LONGLONG previous = -1, lastDuration = 0;
        while (auto sample = source.Next()) {
            LONGLONG time = 0;
            Check(sample->GetSampleTime(&time), L"Timestamp test");
            Check(sample->GetSampleDuration(&lastDuration), L"Durée test");
            Expect(time > previous && lastDuration > 0 && (count || time == 0));
            previous = time;
            ++count;
        }
        Expect(count == source.Count() && previous + lastDuration == source.duration);
        source.Rewind();
        auto restart = source.Next();
        LONGLONG time = -1;
        Check(restart->GetSampleTime(&time), L"Rewind test");
        Expect(time == 0);
        BoundedMp4 compact(argv[1], 4);
        Expect(compact.BufferCount() == 4 && compact.allocatedBytes < source.allocatedBytes);
        std::array<ComPtr<IMFSample>, 4> compactHeld;
        for (auto& sample : compactHeld)
            sample = compact.Next();
        exhausted = false;
        try {
            auto extra = compact.Next();
        } catch (const Failure& failure) {
            exhausted = failure.hr == MF_E_NOTACCEPTING;
        }
        Expect(exhausted);
        compactHeld[0].Reset();
        auto compactNext = compact.Next();
        Expect(compactNext != nullptr);
        bool reordered = false;
        try {
            BoundedMp4 invalid(argv[2]);
        } catch (const Failure& failure) {
            reordered = failure.hr == MF_E_INVALIDMEDIATYPE;
        }
        Expect(reordered);
        RejectFixture({});
        RejectFixture({0, 0, 0, 8, 'm', 'o', 'o', 'v'});
        RejectFixture({0, 0, 0, 1, 'm', 'o', 'o', 'v'});
        RejectFixture({255, 255, 255, 255, 'm', 'o', 'o', 'v'});
        printf("PASS: Annex B, timestamp/duration, complete read and rewind, retained-buffer protection, "
               "reordered input rejection, four malformed boxes\n");
    } catch (const Failure& failure) {
        fwprintf(stderr, L"FAIL: %s 0x%08X\n", failure.operation, unsigned(failure.hr));
        result = 1;
    } catch (...) {
        fwprintf(stderr, L"FAIL: unexpected exception\n");
        result = 1;
    }
    if (SUCCEEDED(mf))
        MFShutdown();
    if (SUCCEEDED(com))
        CoUninitialize();
    return result;
}
