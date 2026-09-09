// Virtual Screen Recorder - test-only media validator (design.md §8.4).
//
// Inspects an MP4 through the Media Foundation Source Reader:
//   - container opens and exposes an H.264 video stream
//   - reports dimensions and nominal frame rate
//   - verifies sample timestamps are strictly monotonic
//   - decodes every frame from beginning to end
//   - reports duration (sum of last timestamp + duration)
// Not shipped with the product (design.md §8.4).
//
// Usage: MediaValidator.exe <file.mp4> [--expect-width W] [--expect-height H]
//                            [--expect-fps FPS] [--expect-duration-sec S]
//                            [--tolerance-sec T]
// Exit code 0 = valid; non-zero = invalid (details on stdout).

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfobjects.h>
#include <wrl/client.h>

#include <cwchar>
#include <cstdio>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {

struct Options {
    std::wstring file;
    UINT32 expectWidth = 0;
    UINT32 expectHeight = 0;
    double expectFps = 0.0;
    double expectDurationSec = 0.0;
    double toleranceSec = 2.0;
};

Options ParseArgs(int argc, wchar_t** argv) {
    Options o;
    if (argc >= 2) o.file = argv[1];
    for (int i = 2; i + 1 < argc; i += 2) {
        if (wcscmp(argv[i], L"--expect-width") == 0) o.expectWidth = static_cast<UINT32>(_wtoi(argv[i + 1]));
        else if (wcscmp(argv[i], L"--expect-height") == 0) o.expectHeight = static_cast<UINT32>(_wtoi(argv[i + 1]));
        else if (wcscmp(argv[i], L"--expect-fps") == 0) o.expectFps = _wtof(argv[i + 1]);
        else if (wcscmp(argv[i], L"--expect-duration-sec") == 0) o.expectDurationSec = _wtof(argv[i + 1]);
        else if (wcscmp(argv[i], L"--tolerance-sec") == 0) o.toleranceSec = _wtof(argv[i + 1]);
    }
    return o;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const Options opt = ParseArgs(argc, argv);
    if (opt.file.empty()) {
        wprintf(L"usage: MediaValidator.exe <file.mp4> [--expect-width W] [--expect-height H] "
                L"[--expect-fps FPS] [--expect-duration-sec S] [--tolerance-sec T]\n");
        return 2;
    }

    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
        wprintf(L"FAIL: MFStartup\n");
        return 2;
    }

    ComPtr<IMFSourceReader> reader;
    {
        ComPtr<IMFAttributes> attrs;
        MFCreateAttributes(&attrs, 1);
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, 0);
        const HRESULT hr = MFCreateSourceReaderFromURL(opt.file.c_str(), attrs.Get(), &reader);
        if (FAILED(hr)) {
            wprintf(L"FAIL: container does not open (0x%08X)\n", static_cast<unsigned>(hr));
            MFShutdown();
            return 1;
        }
    }

    // Select the first video stream, RGB output for decode verification.
    ComPtr<IMFMediaType> type;
    HRESULT hr = MFCreateMediaType(&type);
    if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (SUCCEEDED(hr)) {
        hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type.Get());
    }
    if (FAILED(hr)) {
        wprintf(L"FAIL: no decodable video stream (0x%08X)\n", static_cast<unsigned>(hr));
        MFShutdown();
        return 1;
    }

    // Native media type: dimensions and frame rate.
    ComPtr<IMFMediaType> native;
    if (FAILED(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &native))) {
        wprintf(L"FAIL: current media type\n");
        MFShutdown();
        return 1;
    }
    UINT32 width = 0, height = 0;
    MFGetAttributeSize(native.Get(), MF_MT_FRAME_SIZE, &width, &height);
    UINT32 fpsNum = 0;
    UINT32 fpsDen = 1;
    MFGetAttributeRatio(native.Get(), MF_MT_FRAME_RATE, &fpsNum, &fpsDen);
    const double fps = fpsDen != 0 ? static_cast<double>(fpsNum) / fpsDen : 0.0;

    // Check the stream is H.264 in the container.
    ComPtr<IMFMediaType> decoded;
    GUID subtype{};
    bool isH264 = false;
    for (DWORD i = 0; SUCCEEDED(reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), i, &decoded)); ++i) {
        if (SUCCEEDED(decoded->GetGUID(MF_MT_SUBTYPE, &subtype)) && subtype == MFVideoFormat_H264) {
            isH264 = true;
            break;
        }
        decoded.Reset();
    }

    wprintf(L"media: %ux%u @ %.3f fps, h264=%d\n", width, height, fps, isH264 ? 1 : 0);

    int failures = 0;
    if (!isH264) {
        wprintf(L"FAIL: stream is not H.264 (FR-050)\n");
        ++failures;
    }
    if (opt.expectWidth != 0 && width != opt.expectWidth) {
        wprintf(L"FAIL: width %u != expected %u\n", width, opt.expectWidth);
        ++failures;
    }
    if (opt.expectHeight != 0 && height != opt.expectHeight) {
        wprintf(L"FAIL: height %u != expected %u\n", height, opt.expectHeight);
        ++failures;
    }
    if (opt.expectFps > 0.0 && (fps < opt.expectFps - 0.5 || fps > opt.expectFps + 0.5)) {
        wprintf(L"FAIL: fps %.3f != expected %.3f\n", fps, opt.expectFps);
        ++failures;
    }

    // Walk every sample: monotonic timestamps (FR-055) + full decode.
    LONGLONG lastTime = -1;
    LONGLONG lastTs = 0;
    LONGLONG lastDur = 0;
    unsigned frames = 0;
    for (;;) {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags,
                                nullptr, &sample);
        if (FAILED(hr)) {
            wprintf(L"FAIL: decode error at frame %u (0x%08X)\n", frames, static_cast<unsigned>(hr));
            ++failures;
            break;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (sample == nullptr) continue;

        LONGLONG t = 0, d = 0;
        sample->GetSampleTime(&t);
        sample->GetSampleDuration(&d);
        if (t <= lastTime) {
            wprintf(L"FAIL: non-monotonic timestamp at frame %u (%lld <= %lld)\n", frames, t,
                    lastTime);
            ++failures;
            break;
        }
        lastTime = t;
        lastTs = t;
        lastDur = d;
        ++frames;
    }

    const double durationSec = static_cast<double>(lastTs + lastDur) / 10'000'000.0;
    wprintf(L"decoded frames: %u, duration: %.3f s\n", frames, durationSec);

    if (frames == 0) {
        wprintf(L"FAIL: no frames decoded\n");
        ++failures;
    }
    if (opt.expectDurationSec > 0.0) {
        const double delta = durationSec - opt.expectDurationSec;
        if (delta < -opt.toleranceSec || delta > opt.toleranceSec) {
            wprintf(L"FAIL: duration %.3f s outside %.1f +/- %.1f s\n", durationSec,
                    opt.expectDurationSec, opt.toleranceSec);
            ++failures;
        }
    }

    MFShutdown();
    wprintf(failures == 0 ? L"VALID\n" : L"INVALID\n");
    return failures == 0 ? 0 : 1;
}
