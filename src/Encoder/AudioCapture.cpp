#include "AudioCapture.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>

#include <Infrastructure/Log.h>

#include <chrono>
#include <cmath>
#pragma comment(lib, "ole32.lib")

namespace vsr::encoder {

namespace {

// Minimal IMMNotificationClient is unnecessary; we only query the default
// endpoint once at start. Endpoint changes mid-recording keep the old stream
// (documented limitation for the MVP).
class FormatProbe {
public:
    static Result<AudioFormat> FromDefaultEndpoint() {
        IMMDeviceEnumerator* enumerator = nullptr;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      __uuidof(IMMDeviceEnumerator),
                                      reinterpret_cast<void**>(&enumerator));
        if (FAILED(hr) || enumerator == nullptr) {
            return ErrorFromHresult(hr, ErrorCode::EncoderWriteFailed,
                                     L"System audio is not available on this machine.",
                                     L"CoCreateInstance(MMDeviceEnumerator) failed", true);
        }
        IMMDevice* device = nullptr;
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        enumerator->Release();
        if (FAILED(hr) || device == nullptr) {
            return ErrorFromHresult(hr, ErrorCode::EncoderWriteFailed,
                                     L"No default audio output device was found.",
                                     L"GetDefaultAudioEndpoint failed", true);
        }
        IAudioClient* client = nullptr;
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(&client));
        device->Release();
        if (FAILED(hr) || client == nullptr) {
            return ErrorFromHresult(hr, ErrorCode::EncoderWriteFailed,
                                     L"System audio is not available on this machine.",
                                     L"IAudioClient activate failed", true);
        }
        WAVEFORMATEX* mix = nullptr;
        hr = client->GetMixFormat(&mix);
        AudioFormat fmt;
        if (SUCCEEDED(hr) && mix != nullptr) {
            fmt.sampleRate = mix->nSamplesPerSec;
            fmt.channels = mix->nChannels;
        }
        if (mix != nullptr) CoTaskMemFree(mix);
        client->Release();
        if (FAILED(hr)) {
            return ErrorFromHresult(hr, ErrorCode::EncoderWriteFailed,
                                     L"System audio is not available on this machine.",
                                     L"GetMixFormat failed", true);
        }
        return fmt;
    }
};

} // namespace

Result<AudioFormat> AudioCapture::QueryLoopbackFormat() {
    return FormatProbe::FromDefaultEndpoint();
}

AudioCapture::AudioCapture(const IClock& clock, SinkFn sink)
    : clock_(clock), sink_(std::move(sink)) {}

AudioCapture::~AudioCapture() {
    Stop();
}

Result<std::unique_ptr<AudioCapture>> AudioCapture::Start(const IClock& clock, SinkFn sink) {
    // Validate loopback availability up front so callers can fall back to
    // video-only before any file exists.
    auto fmt = QueryLoopbackFormat();
    if (!fmt) return fmt.TakeError();

    auto* capture = new AudioCapture(clock, std::move(sink));
    capture->thread_ = std::thread([capture] { capture->CaptureLoop(); });
    log::Info("Encoder", "AUDIO_CAPTURE_STARTED", L"", L"System audio loopback capture started.",
              {{"sampleRate", std::to_string(fmt.Value().sampleRate)},
               {"channels", std::to_string(fmt.Value().channels)}});
    return std::unique_ptr<AudioCapture>(capture);
}

void AudioCapture::Stop() noexcept {
    bool expected = false;
    if (!stopped_.compare_exchange_strong(expected, true)) return;
    if (thread_.joinable()) thread_.join();
}

void AudioCapture::CaptureLoop() {
    bool comInitializedHere = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    WAVEFORMATEX* mix = nullptr;

    auto cleanup = [&]() {
        if (mix != nullptr) CoTaskMemFree(mix);
        if (capture != nullptr) capture->Release();
        if (client != nullptr) client->Release();
        if (device != nullptr) device->Release();
        if (enumerator != nullptr) enumerator->Release();
        if (comInitializedHere) CoUninitialize();
    };

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) { cleanup(); return; }
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) { cleanup(); return; }
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(&client));
    if (FAILED(hr)) { cleanup(); return; }
    hr = client->GetMixFormat(&mix);
    if (FAILED(hr) || mix == nullptr) { cleanup(); return; }

    if (mix != nullptr) {
        const bool ext = mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE;
        log::Info("Encoder", "AUDIO_MIX_FORMAT", L"", L"Loopback mix format.",
                  {{"tag", std::to_string(mix->wFormatTag)},
                   {"bits", std::to_string(mix->wBitsPerSample)},
                   {"channels", std::to_string(mix->nChannels)},
                   {"rate", std::to_string(mix->nSamplesPerSec)},
                   {"extensible", ext ? "1" : "0"}});
    }

    // Explicit PCM16 capture format with engine-side conversion
    // (AUTOCONVERTPCM + SRC): the engine resamples and converts its float mix
    // to 16-bit PCM for us. Hand-parsing the float mix format produced noise
    // on real meeting audio for one user while probe tones stayed clean; the
    // engine conversion removes that whole ambiguity, matching how Windows'
    // own recorders consume loopback.
    WAVEFORMATEX captureFormat = {};
    captureFormat.wFormatTag = WAVE_FORMAT_PCM;
    captureFormat.nChannels = mix->nChannels > 0 ? mix->nChannels : 2;
    captureFormat.nSamplesPerSec = mix->nSamplesPerSec > 0 ? mix->nSamplesPerSec : 48000;
    captureFormat.wBitsPerSample = 16;
    captureFormat.nBlockAlign =
        captureFormat.nChannels * captureFormat.wBitsPerSample / 8;
    captureFormat.nAvgBytesPerSec =
        captureFormat.nSamplesPerSec * captureFormat.nBlockAlign;
    // 20 s buffer budget; the loop drains every ~10 ms so actual latency is low.
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                            20'000'000 /* 2 s in 100ns */, 0, &captureFormat, nullptr);
    if (FAILED(hr)) { cleanup(); return; }
    log::Info("Encoder", "AUDIO_CAPTURE_FORMAT", L"", L"Loopback capture format (engine-converted).",
              {{"channels", std::to_string(captureFormat.nChannels)},
               {"rate", std::to_string(captureFormat.nSamplesPerSec)},
               {"bits", std::to_string(captureFormat.wBitsPerSample)}});

    // Ground-truth dump of the converted capture stream (diagnostics only;
    // set VSR_AUDIO_DUMP=path before recording a suspect session).
    wchar_t dumpPath[MAX_PATH] = {};
    FILE* dump = nullptr;
    if (GetEnvironmentVariableW(L"VSR_AUDIO_DUMP", dumpPath, MAX_PATH) > 0 &&
        dumpPath[0] != 0) {
        dump = nullptr;
        _wfopen_s(&dump, dumpPath, L"wb");
    }
    hr = client->GetService(__uuidof(IAudioCaptureClient),
                            reinterpret_cast<void**>(&capture));
    if (FAILED(hr) || capture == nullptr) { cleanup(); return; }
    hr = client->Start();
    if (FAILED(hr)) { cleanup(); return; }

    std::vector<std::int16_t> pcm;

    while (!stopped_.load(std::memory_order_acquire)) {
        // Drain EVERY queued packet in one cycle: reading slower than the
        // device period overflows the WASAPI buffer and corrupts the audio
        // (observed with the small-period RDP audio endpoint).
        std::vector<BYTE> pending;
        UINT32 packet = 0;
        for (;;) {
            if (FAILED(capture->GetNextPacketSize(&packet))) return;
            if (packet == 0) break;
            BYTE* data = nullptr;
            UINT32 bytes = 0;
            DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&data, &bytes, &flags, nullptr, nullptr))) return;
            if (data != nullptr && bytes > 0 && !(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                pending.insert(pending.end(), data, data + bytes);
            } else {
                pending.insert(pending.end(), bytes, 0);
            }
            capture->ReleaseBuffer(bytes);
        }
        if (pending.empty()) {
            // Endpoint idle: poll gently (well under the 2 s WASAPI buffer).
            Sleep(3);
            continue;
        }

        if (dump != nullptr) fwrite(pending.data(), 1, pending.size(), dump);

        AudioChunk chunk;
        chunk.pcm.resize(pending.size() / 2);
        memcpy(chunk.pcm.data(), pending.data(), chunk.pcm.size() * 2);
        chunk.frames = chunk.pcm.size() / (captureFormat.nChannels > 0 ? captureFormat.nChannels : 2);
        // Timestamp the span as (now - duration) so it aligns with video
        // frames captured during the same wall-clock window.
        chunk.timestampUs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(clock_.MonotonicNow()).count()) -
            (chunk.frames * 1000000ULL) / (captureFormat.nSamplesPerSec > 0 ? captureFormat.nSamplesPerSec : 1);
        if (!chunk.pcm.empty() && sink_) sink_(std::move(chunk));
    }

    if (dump != nullptr) fclose(dump);
    client->Stop();
    cleanup();
}

} // namespace vsr::encoder