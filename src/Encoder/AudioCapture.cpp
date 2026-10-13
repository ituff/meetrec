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

// Converts the loopback mix chunk (float32 interleaved or PCM16 depending on
// the mix format) to interleaved 16-bit PCM.
void ConvertToPcm16(const BYTE* data, UINT32 bytes, bool isFloat, std::vector<std::int16_t>& out) {
    out.clear();
    if (isFloat) {
        const auto* f = reinterpret_cast<const float*>(data);
        const std::size_t count = bytes / sizeof(float);
        out.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            float v = f[i];
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            out[i] = static_cast<std::int16_t>(std::lrintf(v * 32767.0f));
        }
    } else if (bytes % 2 == 0) {
        const auto* s = reinterpret_cast<const std::int16_t*>(data);
        const std::size_t count = bytes / sizeof(std::int16_t);
        out.resize(count);
        for (std::size_t i = 0; i < count; ++i) out[i] = s[i];
    }
}

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

    const bool isFloat = mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE
                             ? reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix)
                                       ->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
                             : mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    // 20 s buffer budget; the loop drains every ~10 ms so actual latency is low.
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                            20'000'000 /* 2 s in 100ns */, 0, mix, nullptr);
    if (FAILED(hr)) { cleanup(); return; }
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

        AudioChunk chunk;
        ConvertToPcm16(pending.data(), static_cast<UINT32>(pending.size()), isFloat, chunk.pcm);
        chunk.frames = chunk.pcm.size() / (mix->nChannels > 0 ? mix->nChannels : 2);
        // Timestamp the span as (now - duration) so it aligns with video
        // frames captured during the same wall-clock window.
        chunk.timestampUs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(clock_.MonotonicNow()).count()) -
            (chunk.frames * 1000000ULL) / (mix->nSamplesPerSec > 0 ? mix->nSamplesPerSec : 1);
        if (!chunk.pcm.empty() && sink_) sink_(std::move(chunk));
    }

    client->Stop();
    cleanup();
}

} // namespace vsr::encoder