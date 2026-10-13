#pragma once
#include "../platform.h"
#include <cstdint>
#include <cstring>
#include <limits>

// Experimental, deliberately narrow: flat avc1 MP4, four-byte NAL lengths,
// progressive H.264 with no composition reordering. Never reads mdat wholesale.
class BoundedMp4 {
    static constexpr uint32_t Tag(char a, char b, char c, char d) {
        return (uint32_t(uint8_t(a)) << 24) | (uint32_t(uint8_t(b)) << 16) | (uint32_t(uint8_t(c)) << 8) |
               uint8_t(d);
    }
    static void Require(bool yes) {
        if (!yes)
            throw Failure{MF_E_INVALIDMEDIATYPE, L"MP4 borné : structure invalide ou profil non supporté"};
    }
    struct Box {
        uint64_t begin = 0, end = 0, data = 0;
        uint32_t kind = 0;
    };
    struct File {
        HANDLE handle = INVALID_HANDLE_VALUE;
        ~File() {
            if (handle != INVALID_HANDLE_VALUE)
                CloseHandle(handle);
        }
    } file;
    uint64_t fileSize = 0;
    std::vector<uint8_t> headers;
    struct Packet {
        uint64_t offset = 0;
        uint32_t size = 0, ticks = 0;
        uint64_t time = 0;
    };
    std::vector<Packet> packets;
    size_t cursor = 0;
    uint32_t timescale = 0, maxPacket = 0;
    class Buffer final : public IMFMediaBuffer {
        std::atomic<ULONG> references{1};
        std::unique_ptr<BYTE[]> bytes;
        DWORD capacity = 0, length = 0;

      public:
        explicit Buffer(DWORD count) : bytes(std::make_unique<BYTE[]>(count)), capacity(count) {}
        bool Free() const {
            return references.load(std::memory_order_acquire) == 1;
        }
        BYTE* Data() {
            return bytes.get();
        }
        DWORD Capacity() const {
            return capacity;
        }
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
            if (!out)
                return E_POINTER;
            *out = nullptr;
            if (iid == __uuidof(IUnknown) || iid == __uuidof(IMFMediaBuffer)) {
                *out = static_cast<IMFMediaBuffer*>(this);
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override {
            return ++references;
        }
        ULONG STDMETHODCALLTYPE Release() override {
            ULONG count = --references;
            if (!count)
                delete this;
            return count;
        }
        HRESULT STDMETHODCALLTYPE Lock(BYTE** data, DWORD* maximum, DWORD* current) override {
            if (!data)
                return E_POINTER;
            *data = bytes.get();
            if (maximum)
                *maximum = capacity;
            if (current)
                *current = length;
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE Unlock() override {
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetCurrentLength(DWORD* value) override {
            if (!value)
                return E_POINTER;
            *value = length;
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE SetCurrentLength(DWORD value) override {
            if (value > capacity)
                return E_INVALIDARG;
            length = value;
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetMaxLength(DWORD* value) override {
            if (!value)
                return E_POINTER;
            *value = capacity;
            return S_OK;
        }
    };
    std::array<ComPtr<Buffer>, 16> buffers;
    UINT activeBuffers = 16;
    void Read(uint64_t offset, void* destination, DWORD length) {
        Require(offset <= fileSize && length <= fileSize - offset);
        LARGE_INTEGER position{};
        position.QuadPart = LONGLONG(offset);
        Check(SetFilePointerEx(file.handle, position, nullptr, FILE_BEGIN)
                  ? S_OK
                  : HRESULT_FROM_WIN32(GetLastError()),
              L"MP4 seek disque");
        DWORD got = 0;
        Check(ReadFile(file.handle, destination, length, &got, nullptr) ? S_OK
                                                                        : HRESULT_FROM_WIN32(GetLastError()),
              L"MP4 lecture disque");
        Require(got == length);
    }
    uint32_t U32(uint64_t offset) {
        uint8_t b[4];
        Read(offset, b, 4);
        return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | b[3];
    }
    uint64_t U64(uint64_t offset) {
        return (uint64_t(U32(offset)) << 32) | U32(offset + 4);
    }
    Box At(uint64_t offset, uint64_t end) {
        Require(offset <= end && end - offset >= 8);
        uint64_t length = U32(offset);
        uint32_t kind = U32(offset + 4);
        uint64_t data = offset + 8;
        if (length == 1) {
            Require(end - offset >= 16);
            length = U64(offset + 8);
            data += 8;
        }
        if (!length)
            length = end - offset;
        Require(length >= data - offset && length <= end - offset);
        return {offset, offset + length, data, kind};
    }
    Box Find(Box parent, uint32_t kind) {
        for (uint64_t p = parent.data; p < parent.end;) {
            Box b = At(p, parent.end);
            if (b.kind == kind)
                return b;
            p = b.end;
        }
        return {};
    }
    uint32_t Word(Box b, uint64_t displacement) {
        Require(b.end && displacement <= b.end - b.data && b.end - b.data - displacement >= 4);
        return U32(b.data + displacement);
    }
    void Table(Box b, uint32_t count, uint32_t stride) {
        Require(b.end && count <= 1000000 && b.end - b.data >= 8 &&
                uint64_t(count) * stride <= b.end - b.data - 8);
    }
    void Parse(Box track) {
        Box mdia = Find(track, Tag('m', 'd', 'i', 'a'));
        Require(mdia.end != 0);
        Box mdhd = Find(mdia, Tag('m', 'd', 'h', 'd'));
        uint32_t version = Word(mdhd, 0) >> 24;
        Require(version <= 1);
        timescale = Word(mdhd, version ? 20 : 12);
        Require(timescale != 0);
        Box stbl = Find(Find(mdia, Tag('m', 'i', 'n', 'f')), Tag('s', 't', 'b', 'l'));
        Require(stbl.end != 0);
        Box stsd = Find(stbl, Tag('s', 't', 's', 'd'));
        Require(Word(stsd, 4) == 1);
        Box entry = At(stsd.data + 8, stsd.end);
        Require(entry.kind == Tag('a', 'v', 'c', '1') && entry.end - entry.begin >= 86);
        uint32_t dimensions = U32(entry.begin + 32);
        width = dimensions >> 16;
        height = dimensions & 65535;
        Require(width && height);
        Box children = entry;
        children.data = entry.begin + 86;
        Box avcc = Find(children, Tag('a', 'v', 'c', 'C'));
        Require(avcc.end && avcc.end - avcc.data >= 7 && avcc.end - avcc.data <= 65536);
        std::vector<uint8_t> config(size_t(avcc.end - avcc.data));
        Read(avcc.data, config.data(), DWORD(config.size()));
        Require(config[0] == 1 && (config[4] & 3) == 3);
        size_t p = 6;
        unsigned count = config[5] & 31;
        Require(count > 0);
        auto append = [&](unsigned n) {
            for (unsigned i = 0; i < n; ++i) {
                Require(p + 2 <= config.size());
                size_t bytes = (size_t(config[p]) << 8) | config[p + 1];
                p += 2;
                Require(bytes && bytes <= config.size() - p);
                headers.insert(headers.end(), {0, 0, 0, 1});
                headers.insert(headers.end(), config.begin() + p, config.begin() + p + bytes);
                p += bytes;
            }
        };
        append(count);
        Require(p < config.size());
        count = config[p++];
        Require(count > 0);
        append(count);
        Box stsz = Find(stbl, Tag('s', 't', 's', 'z'));
        uint32_t constant = Word(stsz, 4), sampleCount = Word(stsz, 8);
        Require(sampleCount && sampleCount <= 1000000);
        if (!constant)
            Require(uint64_t(sampleCount) * 4 <= stsz.end - stsz.data - 12);
        packets.resize(sampleCount);
        for (uint32_t i = 0; i < sampleCount; ++i) {
            uint32_t size = constant ? constant : Word(stsz, 12 + uint64_t(i) * 4);
            Require(size && size <= 2 * 1024 * 1024);
            packets[i].size = size;
            maxPacket = std::max(maxPacket, size);
        }
        Box stts = Find(stbl, Tag('s', 't', 't', 's'));
        count = Word(stts, 4);
        Table(stts, count, 8);
        size_t index = 0;
        uint64_t clock = 0;
        for (unsigned i = 0; i < count; ++i) {
            uint32_t n = Word(stts, 8 + uint64_t(i) * 8), ticks = Word(stts, 12 + uint64_t(i) * 8);
            Require(n && ticks && n <= packets.size() - index);
            for (uint32_t j = 0; j < n; ++j) {
                packets[index].time = clock;
                packets[index++].ticks = ticks;
                clock += ticks;
            }
        }
        Require(index == packets.size() &&
                clock <= uint64_t(std::numeric_limits<LONGLONG>::max() / 10000000));
        duration = LONGLONG(clock * 10000000 / timescale);
        Box ctts = Find(stbl, Tag('c', 't', 't', 's'));
        if (ctts.end) {
            count = Word(ctts, 4);
            Table(ctts, count, 8);
            uint64_t covered = 0;
            for (unsigned i = 0; i < count; ++i) {
                covered += Word(ctts, 8 + uint64_t(i) * 8);
                Require(Word(ctts, 12 + uint64_t(i) * 8) == 0);
            }
            Require(covered == sampleCount);
        }
        Box sync = Find(stbl, Tag('s', 't', 's', 's'));
        std::vector<uint8_t> keys(sampleCount, sync.end ? 0 : 1);
        uint32_t keySpacing = 1;
        if (sync.end) {
            count = Word(sync, 4);
            Table(sync, count, 4);
            Require(count && Word(sync, 8) == 1);
            uint32_t previous = 0;
            keySpacing = sampleCount;
            for (unsigned i = 0; i < count; ++i) {
                uint32_t key = Word(sync, 8 + uint64_t(i) * 4);
                Require(key > previous && key <= sampleCount);
                if (previous)
                    keySpacing = std::min(keySpacing, key - previous);
                keys[key - 1] = 1;
                previous = key;
            }
            keySpacing = std::min(keySpacing, sampleCount - previous + 1);
        }
        Box offsets = Find(stbl, Tag('s', 't', 'c', 'o'));
        bool wide = false;
        if (!offsets.end) {
            offsets = Find(stbl, Tag('c', 'o', '6', '4'));
            wide = true;
        }
        uint32_t chunks = Word(offsets, 4);
        Table(offsets, chunks, wide ? 8 : 4);
        Require(chunks != 0);
        Box stsc = Find(stbl, Tag('s', 't', 's', 'c'));
        uint32_t groups = Word(stsc, 4);
        Table(stsc, groups, 12);
        Require(groups && Word(stsc, 8) == 1);
        uint32_t group = 0, perChunk = Word(stsc, 12);
        Require(perChunk && Word(stsc, 16) == 1);
        index = 0;
        for (uint32_t chunk = 1; chunk <= chunks; ++chunk) {
            if (group + 1 < groups) {
                uint32_t next = Word(stsc, 8 + uint64_t(group + 1) * 12);
                Require(next > Word(stsc, 8 + uint64_t(group) * 12));
                if (chunk == next) {
                    ++group;
                    perChunk = Word(stsc, 12 + uint64_t(group) * 12);
                    Require(perChunk && Word(stsc, 16 + uint64_t(group) * 12) == 1);
                }
            }
            uint64_t offset = wide ? U64(offsets.data + 8 + uint64_t(chunk - 1) * 8)
                                   : Word(offsets, 8 + uint64_t(chunk - 1) * 4);
            Require(perChunk <= packets.size() - index);
            for (uint32_t i = 0; i < perChunk; ++i) {
                Packet& packet = packets[index++];
                Require(offset <= fileSize && packet.size <= fileSize - offset);
                packet.offset = offset;
                offset += packet.size;
            }
        }
        Require(index == packets.size() && group + 1 == groups);
        // The same sixteen compressed buffers are reused only when all decoder
        // references have gone. COM samples may still allocate internally.
        capacity = maxPacket + DWORD(headers.size());
        uint32_t compactCapacity = 0;
        for (size_t i = 0; i < packets.size(); ++i)
            if (!keys[i])
                compactCapacity = std::max(compactCapacity, packets[i].size);
        for (size_t i = 0; i < activeBuffers; ++i) {
            DWORD bytes =
                (keySpacing >= activeBuffers && compactCapacity && i >= 2) ? compactCapacity : capacity;
            buffers[i].Attach(new Buffer(bytes));
            allocatedBytes += bytes;
        }
    }

  public:
    UINT width = 0, height = 0, capacity = 0, allocatedBytes = 0;
    LONGLONG duration = 0;
    explicit BoundedMp4(const std::wstring& path, UINT bufferCount = 16) : activeBuffers(bufferCount) {
        Require(activeBuffers >= 2 && activeBuffers <= buffers.size());
        file.handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(file.handle != INVALID_HANDLE_VALUE ? S_OK : HRESULT_FROM_WIN32(GetLastError()),
              L"Ouvrir MP4 borné");
        LARGE_INTEGER size{};
        Check(GetFileSizeEx(file.handle, &size) ? S_OK : HRESULT_FROM_WIN32(GetLastError()), L"Taille MP4");
        Require(size.QuadPart >= 8);
        fileSize = uint64_t(size.QuadPart);
        Box root{0, fileSize, 0, 0};
        Require(!Find(root, Tag('m', 'o', 'o', 'f')).end);
        Box moov = Find(root, Tag('m', 'o', 'o', 'v'));
        Require(moov.end && moov.end - moov.begin <= 64 * 1024 * 1024 &&
                !Find(moov, Tag('m', 'v', 'e', 'x')).end);
        bool found = false;
        for (uint64_t p = moov.data; p < moov.end;) {
            Box track = At(p, moov.end);
            p = track.end;
            if (track.kind != Tag('t', 'r', 'a', 'k'))
                continue;
            Box mdia = Find(track, Tag('m', 'd', 'i', 'a'));
            Box handler = Find(mdia, Tag('h', 'd', 'l', 'r'));
            if (Word(handler, 8) != Tag('v', 'i', 'd', 'e'))
                continue;
            Require(!found);
            Parse(track);
            found = true;
        }
        Require(found);
    }
    size_t Count() const {
        return packets.size();
    }
    double FrameRate() const {
        return double(timescale) / packets[0].ticks;
    }
    UINT BufferCount() const {
        return activeBuffers;
    }
    void Rewind() {
        cursor = 0;
    }
    ComPtr<IMFMediaType> Type() const {
        ComPtr<IMFMediaType> type;
        Check(MFCreateMediaType(&type), L"Type H264 borné");
        Check(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), L"Major H264");
        Check(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264), L"Subtype H264");
        Check(MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, width, height), L"Dimensions H264");
        Check(MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, timescale, packets[0].ticks),
              L"Cadence H264");
        Check(MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1), L"Aspect H264");
        Check(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), L"Progressif H264");
        return type;
    }
    ComPtr<IMFSample> Next() {
        if (cursor == packets.size())
            return {};
        const Packet& packet = packets[cursor];
        DWORD prefix = cursor ? 0 : DWORD(headers.size());
        Buffer* buffer = nullptr;
        for (auto& candidate : buffers)
            if (candidate && candidate->Free() && candidate->Capacity() >= packet.size + prefix &&
                (!buffer || candidate->Capacity() < buffer->Capacity()))
                buffer = candidate.Get();
        if (!buffer)
            throw Failure{MF_E_NOTACCEPTING, L"Pool de paquets compressés borné épuisé"};
        BYTE* bytes = buffer->Data();
        if (prefix)
            memcpy(bytes, headers.data(), prefix);
        Read(packet.offset, bytes + prefix, packet.size);
        size_t p = prefix, end = prefix + packet.size;
        while (p < end) {
            Require(end - p >= 4);
            uint32_t n = (uint32_t(bytes[p]) << 24) | (uint32_t(bytes[p + 1]) << 16) |
                         (uint32_t(bytes[p + 2]) << 8) | bytes[p + 3];
            Require(n && n <= end - p - 4);
            bytes[p] = bytes[p + 1] = bytes[p + 2] = 0;
            bytes[p + 3] = 1;
            p += 4 + n;
        }
        Check(buffer->SetCurrentLength(DWORD(end)), L"Taille paquet borné");
        ComPtr<IMFSample> sample;
        Check(MFCreateSample(&sample), L"Sample compressé");
        Check(sample->AddBuffer(buffer), L"Buffer compressé");
        Check(sample->SetSampleTime(LONGLONG(packet.time * 10000000 / timescale)), L"Timestamp compressé");
        Check(sample->SetSampleDuration(LONGLONG(uint64_t(packet.ticks) * 10000000 / timescale)),
              L"Durée compressée");
        if (!cursor)
            Check(sample->SetUINT32(MFSampleExtension_CleanPoint, TRUE), L"Première image clé");
        ++cursor;
        return sample;
    }
};
