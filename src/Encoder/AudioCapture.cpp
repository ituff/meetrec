#include "AudioCapture.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <windows.h>

#include <Infrastructure/Log.h>
#include <Infrastructure/Strings.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#pragma comment(lib, "ole32.lib")

namespace vsr::encoder {

namespace {

// PKEY_Device_FriendlyName, spelled out so no extra SDK import lib is needed.
constexpr PROPERTYKEY kPkeyDeviceFriendlyName = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

// Env knobs (all optional):
//   VSR_MIC_GAIN           multiplier on the mic before mixing (default 1.0,
//                          clamped to 0.25..8.0)
//   VSR_MIC_DISABLE=1      skip the microphone even when requested
//   VSR_AUDIO_DUMP         raw mixed stream sent to the writer (s16le PCM)
//   VSR_AUDIO_DUMP_LOOPBACK / VSR_AUDIO_DUMP_MIC
//                          per-source raw feed before mixing (s16le PCM)
constexpr wchar_t kMicGainEnv[] = L"VSR_MIC_GAIN";
constexpr wchar_t kMicDisableEnv[] = L"VSR_MIC_DISABLE";
constexpr wchar_t kDumpEnv[] = L"VSR_AUDIO_DUMP";
constexpr wchar_t kLoopbackDumpEnv[] = L"VSR_AUDIO_DUMP_LOOPBACK";
constexpr wchar_t kMicDumpEnv[] = L"VSR_AUDIO_DUMP_MIC";

constexpr std::uint64_t kUsPerSecond = 1'000'000ULL;

// Frames the per-source timelines retain (~400 ms). A stalled mixer must not
// grow them without bound, and a silent endpoint can stop delivering packets
// for minutes, which must not materialize as a huge silence buffer.
constexpr std::uint64_t kRetentionUs = 400'000;

std::uint64_t MonotonicUs(const IClock& clock) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(clock.MonotonicNow()).count());
}

// QPC ticks to microseconds (the frequency is not necessarily 10 MHz). Split
// the division so long uptimes cannot overflow.
std::uint64_t QpcTicksToUs(std::uint64_t ticks) {
    static const std::uint64_t frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return static_cast<std::uint64_t>(value.QuadPart > 0 ? value.QuadPart : 1);
    }();
    return (ticks / frequency) * kUsPerSecond + (ticks % frequency) * kUsPerSecond / frequency;
}

std::wstring EndpointFriendlyName(IMMDevice* device) {
    IPropertyStore* store = nullptr;
    if (device == nullptr ||
        FAILED(device->OpenPropertyStore(STGM_READ, &store)) || store == nullptr) {
        return L"";
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring name;
    if (SUCCEEDED(store->GetValue(kPkeyDeviceFriendlyName, &value)) &&
        value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
        name = value.pwszVal;
    }
    PropVariantClear(&value);
    store->Release();
    return name;
}

// Opens the requested endpoint, or the default for the roles when no ID is
// set. A saved ID whose device disappeared falls back to the default: a stale
// setting must not leave the recording without that source.
HRESULT OpenEndpointDevice(IMMDeviceEnumerator* enumerator, EDataFlow flow,
                           std::initializer_list<ERole> roles, const std::wstring& deviceId,
                           IMMDevice** device) {
    *device = nullptr;
    HRESULT hr = E_NOTFOUND;
    if (!deviceId.empty()) {
        hr = enumerator->GetDevice(deviceId.c_str(), device);
        if (SUCCEEDED(hr) && *device != nullptr) return hr;
        *device = nullptr;
        log::Warning("Encoder", "AUDIO_DEVICE_FALLBACK", L"",
                     L"The selected audio endpoint is unavailable; using the Windows default.",
                     {{"deviceId", WideToUtf8(deviceId)}});
    }
    for (const ERole role : roles) {
        hr = enumerator->GetDefaultAudioEndpoint(flow, role, device);
        if (SUCCEEDED(hr) && *device != nullptr) break;
    }
    return hr;
}

// Minimal probe: endpoint mix format (rate/channels) per role, in priority
// order. We only query once at start; endpoint changes mid-recording keep the
// old stream (documented limitation for the MVP).
class FormatProbe {
public:
    static Result<AudioFormat> FromEndpoint(EDataFlow flow, std::initializer_list<ERole> roles,
                                            const wchar_t* missingUserMessage,
                                            const std::wstring& deviceId = {}) {
        IMMDeviceEnumerator* enumerator = nullptr;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      __uuidof(IMMDeviceEnumerator),
                                      reinterpret_cast<void**>(&enumerator));
        if (FAILED(hr) || enumerator == nullptr) {
            return ErrorFromHresult(hr, ErrorCode::EncoderWriteFailed,
                                     L"Audio capture is not available on this machine.",
                                     L"CoCreateInstance(MMDeviceEnumerator) failed", true);
        }
        IMMDevice* device = nullptr;
        hr = OpenEndpointDevice(enumerator, flow, roles, deviceId, &device);
        enumerator->Release();
        if (FAILED(hr) || device == nullptr) {
            return ErrorFromHresult(hr, ErrorCode::EncoderWriteFailed, missingUserMessage,
                                     L"GetDefaultAudioEndpoint failed", true);
        }
        IAudioClient* client = nullptr;
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(&client));
        device->Release();
        if (FAILED(hr) || client == nullptr) {
            return ErrorFromHresult(hr, ErrorCode::EncoderWriteFailed,
                                     L"Audio capture is not available on this machine.",
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
                                     L"Audio capture is not available on this machine.",
                                     L"GetMixFormat failed", true);
        }
        return fmt;
    }
};

float MicGainFromEnv(float fallback) {
    wchar_t text[32] = {};
    if (GetEnvironmentVariableW(kMicGainEnv, text, 32) == 0 || text[0] == 0) return fallback;
    const float gain = static_cast<float>(_wtof(text));
    if (!(gain > 0.0f)) return fallback; // NaN / zero / negative -> ignore
    return std::clamp(gain, 0.25f, 8.0f);
}

} // namespace

// Contiguous, timestamped PCM timeline for one source. The source thread
// appends; the mixer thread consumes from the front. Engine gaps are bridged
// with silence so the buffer always covers a contiguous [startUs, endUs_)
// span, which makes cursor alignment exact integer arithmetic.
class AudioCapture::TimelineBuffer {
public:
    TimelineBuffer(std::uint32_t sampleRate, std::uint32_t channels)
        : rate_(sampleRate), channels_(channels) {}

    void Push(std::uint64_t timestampUs, const std::int16_t* pcm, std::size_t frames) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pcm == nullptr || frames == 0 || channels_ == 0) return;
        if (samples_.empty()) {
            startUs_ = timestampUs;
        } else if (timestampUs > endUs_) {
            const std::uint64_t gapUs = timestampUs - endUs_;
            if (gapUs > kRetentionUs) {
                // Long idle: the engine stops delivering packets entirely
                // while the endpoint is silent (observed on 26200), so the
                // gap can be minutes. Bridging it would allocate a huge
                // silence buffer for no benefit - the mixer zero-fills the
                // span it consumed in the meantime. Restart the timeline.
                samples_.clear();
                startUs_ = timestampUs;
            } else {
                // Short gap: the engine skipped this span; bridge with
                // silence so Consume can stay timestamp-naive.
                const std::size_t gap = static_cast<std::size_t>(UsToFrames(gapUs));
                samples_.insert(samples_.end(), gap * channels_, 0);
            }
        } else if (timestampUs < endUs_) {
            // Overlap (clock jitter between pushes): clip the covered prefix.
            const std::size_t overlap = static_cast<std::size_t>(UsToFrames(endUs_ - timestampUs));
            if (overlap >= frames) {
                droppedFrames_ += frames;
                return;
            }
            pcm += overlap * channels_;
            frames -= overlap;
        }
        samples_.insert(samples_.end(), pcm, pcm + frames * channels_);
        endUs_ = startUs_ + FramesToUs(samples_.size() / channels_);

        // Latency bound: a stalled mixer thread must not grow this buffer
        // without limit; keep at most ~400 ms by dropping the oldest frames.
        if (endUs_ - startUs_ > kRetentionUs) {
            const std::uint64_t excess = UsToFrames(endUs_ - startUs_ - kRetentionUs);
            const std::size_t avail = samples_.size() / channels_;
            const std::size_t drop =
                static_cast<std::size_t>(excess) < avail ? static_cast<std::size_t>(excess) : avail;
            samples_.erase(samples_.begin(),
                           samples_.begin() + static_cast<std::ptrdiff_t>(drop * channels_));
            startUs_ += FramesToUs(drop);
            droppedFrames_ += drop;
        }
    }

    // Removes frames that precede the mixer cursor (only possible after a
    // mixer stall left stale data behind).
    void DropBefore(std::uint64_t cursorUs) {
        std::lock_guard<std::mutex> lock(mutex_);
        DropBeforeLocked(cursorUs);
    }

    // Copies up to maxFrames from the front; returns how many were taken.
    std::size_t Consume(std::int16_t* dst, std::size_t maxFrames) {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::size_t avail = samples_.size() / channels_;
        const std::size_t n = avail < maxFrames ? avail : maxFrames;
        if (n > 0) {
            std::copy(samples_.begin(),
                      samples_.begin() + static_cast<std::ptrdiff_t>(n * channels_), dst);
            samples_.erase(samples_.begin(),
                           samples_.begin() + static_cast<std::ptrdiff_t>(n * channels_));
            startUs_ += FramesToUs(n);
            if (samples_.empty()) startUs_ = endUs_;
        }
        return n;
    }

    bool Empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return samples_.empty();
    }

    std::uint64_t StartUs() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return startUs_;
    }

    std::uint64_t DroppedFrames() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return droppedFrames_;
    }

    std::uint64_t FramesToUs(std::uint64_t frames) const {
        return frames * kUsPerSecond / rate_;
    }
    std::uint64_t UsToFrames(std::uint64_t us) const {
        return us * rate_ / kUsPerSecond;
    }

private:
    void DropBeforeLocked(std::uint64_t cursorUs) {
        if (cursorUs <= startUs_ || samples_.empty()) return;
        const std::uint64_t want = UsToFrames(cursorUs - startUs_);
        const std::size_t avail = samples_.size() / channels_;
        const std::size_t drop = want < avail ? static_cast<std::size_t>(want) : avail;
        samples_.erase(samples_.begin(),
                       samples_.begin() + static_cast<std::ptrdiff_t>(drop * channels_));
        startUs_ += FramesToUs(drop);
        if (samples_.empty()) startUs_ = endUs_;
    }

    mutable std::mutex mutex_;
    std::deque<std::int16_t> samples_; // interleaved, covers [startUs_, endUs_)
    std::uint64_t startUs_ = 0;
    std::uint64_t endUs_ = 0;
    std::uint64_t droppedFrames_ = 0;
    std::uint32_t rate_;
    std::uint32_t channels_;
};

Result<AudioFormat> AudioCapture::QueryLoopbackFormat(const std::wstring& deviceId) {
    return FormatProbe::FromEndpoint(eRender, {eConsole},
                                     L"No default audio output device was found.", deviceId);
}

Result<AudioFormat> AudioCapture::QueryMicrophoneFormat(const std::wstring& deviceId) {
    // Call apps (Teams, Zoom, Meet) open the communications endpoint by
    // default; the plain console default is the fallback when the roles are
    // separated.
    return FormatProbe::FromEndpoint(eCapture, {eCommunications, eConsole},
                                     L"No default microphone was found.", deviceId);
}

AudioCapture::AudioCapture(const IClock& clock, SinkFn sink, AudioCaptureOptions options)
    : clock_(clock), sink_(std::move(sink)), options_(options) {}

AudioCapture::~AudioCapture() {
    Stop();
}

Result<std::unique_ptr<AudioCapture>> AudioCapture::Start(const IClock& clock, SinkFn sink,
                                                          const AudioCaptureOptions& options) {
    // Env override first so VSR_MIC_DISABLE can force a video+system-only run.
    AudioCaptureOptions effective = options;
    wchar_t flag[8] = {};
    if (GetEnvironmentVariableW(kMicDisableEnv, flag, 8) > 0 && _wcsicmp(flag, L"1") == 0) {
        effective.microphone = false;
    }
    effective.micGain = MicGainFromEnv(effective.micGain);

    if (!effective.loopback && !effective.microphone) {
        return Result<std::unique_ptr<AudioCapture>>{Error{
            ErrorCode::EncoderWriteFailed, L"No audio source is enabled for recording.",
            L"AudioCapture::Start: loopback and microphone are both disabled"}};
    }

    // Resolve the mixed-stream format up front so callers can fall back to
    // video-only before any file exists. Loopback wins when both are
    // available (it anchors the A/V alignment the coordinator relies on).
    bool loopbackOk = false;
    bool micOk = false;
    Error probeError{};
    AudioFormat format{};
    if (effective.loopback) {
        auto fmt = QueryLoopbackFormat(effective.loopbackDeviceId);
        if (fmt) {
            format = fmt.Value();
            loopbackOk = true;
        } else {
            probeError = fmt.TakeError();
        }
    }
    if (effective.microphone) {
        auto fmt = QueryMicrophoneFormat(effective.microphoneDeviceId);
        if (fmt) {
            micOk = true;
            if (!loopbackOk) format = fmt.Value();
        } else if (probeError.code == ErrorCode::None) {
            // Loopback disabled or already failed: surface the mic error
            // instead of a default-constructed (message-less) Error.
            probeError = fmt.TakeError();
        }
    }
    if (!loopbackOk && !micOk) {
        // Surface the loopback probe error: it is the primary source.
        return Result<std::unique_ptr<AudioCapture>>{probeError};
    }
    if (format.sampleRate == 0 || format.channels == 0) {
        format.sampleRate = format.sampleRate > 0 ? format.sampleRate : 48000;
        format.channels = format.channels > 0 ? format.channels : 2;
    }

    auto* capture = new AudioCapture(clock, std::move(sink), effective);
    capture->format_ = format;
    capture->loopbackBuf_ = std::make_unique<TimelineBuffer>(format.sampleRate, format.channels);
    capture->micBuf_ = std::make_unique<TimelineBuffer>(format.sampleRate, format.channels);
    if (loopbackOk) {
        capture->loopbackThread_ = std::thread([capture] { capture->SourceLoop(false); });
    }
    if (micOk) {
        capture->micThread_ = std::thread([capture] { capture->SourceLoop(true); });
    }
    capture->mixerThread_ = std::thread([capture] { capture->MixerLoop(); });

    log::Info("Encoder", "AUDIO_CAPTURE_STARTED", L"",
              L"Meeting audio capture started (loopback + microphone mix).",
              {{"sampleRate", std::to_string(format.sampleRate)},
               {"channels", std::to_string(format.channels)},
               {"loopback", loopbackOk ? "1" : "0"},
               {"mic", micOk ? "1" : "0"},
               {"micGain", std::to_string(effective.micGain)}});
    return std::unique_ptr<AudioCapture>(capture);
}

void AudioCapture::Stop() noexcept {
    bool expected = false;
    if (!stopped_.compare_exchange_strong(expected, true)) return;
    if (loopbackThread_.joinable()) loopbackThread_.join();
    if (micThread_.joinable()) micThread_.join();
    if (mixerThread_.joinable()) mixerThread_.join();
}

// One WASAPI source: opens the endpoint, initializes a shared PCM16 stream
// (engine-converted: rate, channel count, and float->int16 are all done by
// the audio engine, matching how Windows' own recorders consume audio) and
// drains packets into the timeline until Stop().
void AudioCapture::SourceLoop(bool mic) {
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
    auto fail = [&](const char* what, HRESULT hr) {
        wchar_t msg[128] = {};
        _snwprintf_s(msg, _TRUNCATE, L"%hs failed (hr=0x%08lX)", what,
                     static_cast<unsigned long>(hr));
        log::Warning("Encoder", mic ? "AUDIO_MIC_SOURCE_EXITED" : "AUDIO_LOOPBACK_SOURCE_EXITED",
                     L"", msg, {});
        cleanup();
    };

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr)) { fail("CoCreateInstance(MMDeviceEnumerator)", hr); return; }
    if (mic) {
        hr = OpenEndpointDevice(enumerator, eCapture, {eCommunications, eConsole},
                                options_.microphoneDeviceId, &device);
    } else {
        hr = OpenEndpointDevice(enumerator, eRender, {eConsole},
                                options_.loopbackDeviceId, &device);
    }
    if (FAILED(hr)) { fail("GetDefaultAudioEndpoint", hr); return; }
    const std::wstring deviceName = EndpointFriendlyName(device);
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(&client));
    if (FAILED(hr)) { fail("IAudioClient activate", hr); return; }
    hr = client->GetMixFormat(&mix);
    if (FAILED(hr) || mix == nullptr) { fail("GetMixFormat", hr); return; }

    {
        const bool ext = mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE;
        log::Info("Encoder", mic ? "AUDIO_MIC_MIX_FORMAT" : "AUDIO_LOOPBACK_MIX_FORMAT", L"",
                  L"Endpoint mix format.",
                  {{"device", WideToUtf8(deviceName)},
                   {"tag", std::to_string(mix->wFormatTag)},
                   {"bits", std::to_string(mix->wBitsPerSample)},
                   {"channels", std::to_string(mix->nChannels)},
                   {"rate", std::to_string(mix->nSamplesPerSec)},
                   {"extensible", ext ? "1" : "0"}});
    }

    // Explicit PCM16 capture format at the mixed-stream rate/channels; the
    // engine converts (AUTOCONVERTPCM + SRC), including mono->stereo upmix
    // for the mic. Hand-parsing the float mix format produced noise on real
    // meeting audio while probe tones stayed clean; the engine conversion
    // removes that whole ambiguity.
    WAVEFORMATEX captureFormat = {};
    captureFormat.wFormatTag = WAVE_FORMAT_PCM;
    captureFormat.nChannels = static_cast<WORD>(format_.channels);
    captureFormat.nSamplesPerSec = format_.sampleRate;
    captureFormat.wBitsPerSample = 16;
    captureFormat.nBlockAlign =
        static_cast<WORD>(captureFormat.nChannels * captureFormat.wBitsPerSample / 8);
    captureFormat.nAvgBytesPerSec = captureFormat.nSamplesPerSec * captureFormat.nBlockAlign;

    DWORD streamFlags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    if (!mic) streamFlags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
    // 20 s buffer budget (2 s in 100ns units); the loop drains every ~10 ms
    // so actual latency is low.
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, 20'000'000, 0,
                            &captureFormat, nullptr);
    if (FAILED(hr)) { fail("IAudioClient::Initialize", hr); return; }

    // Ground-truth dumps of the raw feeds / mixed stream (diagnostics only).
    auto openDump = [](const wchar_t* env, FILE** out) {
        wchar_t path[MAX_PATH] = {};
        if (GetEnvironmentVariableW(env, path, MAX_PATH) > 0 && path[0] != 0) {
            _wfopen_s(out, path, L"wb");
        }
    };
    FILE* dump = nullptr;
    if (mic) {
        openDump(kMicDumpEnv, &dump);
    } else {
        openDump(kLoopbackDumpEnv, &dump);
    }

    hr = client->GetService(__uuidof(IAudioCaptureClient),
                            reinterpret_cast<void**>(&capture));
    if (FAILED(hr) || capture == nullptr) { fail("GetService(IAudioCaptureClient)", hr); return; }
    hr = client->Start();
    if (FAILED(hr)) { fail("IAudioClient::Start", hr); return; }

    TimelineBuffer& timeline = mic ? *micBuf_ : *loopbackBuf_;
    std::vector<BYTE> silence; // expanded silent packets (push + diagnostics dump)

    // IAudioCaptureClient reports packet sizes in FRAMES (one frame is one
    // sample per channel), and ReleaseBuffer takes the same count back. The
    // packet occupies frames * blockAlign bytes in memory, so appending only
    // `frames` bytes keeps 1/blockAlign of each packet (1/8 for the float32
    // mix, 1/4 for PCM16 stereo) and starves the recording to a faint buzz -
    // the reported "only a bit of hiss" symptom. Always expand by blockAlign.
    const std::size_t blockAlign = captureFormat.nBlockAlign;

    // QPC -> clock-monotonic offset, sampled once: both are QPC-backed.
    LARGE_INTEGER qpcNow{};
    QueryPerformanceCounter(&qpcNow);
    const std::uint64_t qpcOffsetUs =
        MonotonicUs(clock_) - QpcTicksToUs(static_cast<std::uint64_t>(qpcNow.QuadPart));

    // Packet placement. GetBuffer's QPC position locates the first frame of
    // the packet on the audio engine's clock and consecutive packets are
    // contiguous by construction. Timestamping from *drain* times (when this
    // thread happened to wake) inserted a 1-5 ms hole wherever the polling
    // phase moved - audible as a rough buzz on every recording. Anchor on
    // QPC, then advance by sample count; a QPC jump beyond one packet plus
    // tolerance is a real gap (idle endpoint, device glitch) and re-anchors.
    const std::uint32_t rate = format_.sampleRate;
    bool anchored = false;
    std::uint64_t nextStartUs = 0;
    std::uint64_t lastQpcUs = 0;
    std::uint64_t lastDurationUs = 0;

    // Delivery watchdog: some machines' audio engines (third-party DSP/APO
    // software or audio-driver bugs, observed with Waves MaxAudio on Insider
    // builds) deliver only a fraction of the real-time samples. The recording
    // then has silent gaps no in-app mixing can repair, so measure and report
    // it explicitly instead of failing silently.
    std::uint64_t pushedFrames = 0; // cumulative, for the log line
    std::uint64_t windowFrames = 0; // frames since the last ratio check
    std::uint64_t lastCheckMs = MonotonicUs(clock_) / 1000;
    std::uint64_t lastDataMs = lastCheckMs;
    std::uint64_t lastWarnMs = 0;
    auto checkDelivery = [&](std::uint64_t nowUs, std::size_t packetFrames) {
        const std::uint64_t nowMs = nowUs / 1000;
        // The endpoint stops delivering packets entirely while it is idle
        // (no app rendering), so a silent stretch is not slow delivery:
        // restart the measurement window instead of counting it against the
        // endpoint.
        if (nowMs - lastDataMs > 2'000) {
            lastCheckMs = nowMs;
            windowFrames = 0;
        }
        lastDataMs = nowMs;
        windowFrames += packetFrames;
        if (nowMs - lastCheckMs < 5'000) return;
        const std::uint64_t elapsedMs = nowMs - lastCheckMs;
        lastCheckMs = nowMs;
        const double deliveredSeconds =
            static_cast<double>(windowFrames) / static_cast<double>(format_.sampleRate);
        windowFrames = 0;
        const double wallSeconds = static_cast<double>(elapsedMs) / 1000.0;
        const double ratio = wallSeconds > 0 ? deliveredSeconds / wallSeconds : 1.0;
        if (ratio >= 0.85) return;
        // First breach warns immediately; repeats are throttled to 30 s.
        if (lastWarnMs != 0 && nowMs - lastWarnMs < 30'000) return;
        lastWarnMs = nowMs;
        log::Warning("Encoder", "AUDIO_SLOW_DELIVERY", L"",
                     L"Audio endpoint delivers far fewer samples than real time; the "
                     L"recording will have silent gaps. Likely a third-party audio DSP "
                     L"or audio-driver issue (e.g. Waves MaxAudio / audio enhancements). "
                     L"Disable audio enhancements or update the audio driver.",
                     {{"source", mic ? "mic" : "loopback"},
                      {"deliveredSeconds", std::to_string(deliveredSeconds)},
                      {"wallSeconds", std::to_string(wallSeconds)},
                      {"ratio", std::to_string(ratio)},
                      {"totalFrames", std::to_string(pushedFrames)}});
    };

    while (!stopped_.load(std::memory_order_acquire)) {
        // Drain EVERY queued packet in one cycle: reading slower than the
        // device period overflows the WASAPI buffer and corrupts the audio
        // (observed with the small-period RDP audio endpoint).
        UINT32 packetFrames = 0;
        bool drained = false;
        for (;;) {
            HRESULT packetHr = capture->GetNextPacketSize(&packetFrames);
            if (FAILED(packetHr)) { fail("GetNextPacketSize", packetHr); return; }
            if (packetFrames == 0) break;
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 qpcPosition = 0;
            const HRESULT bufferHr =
                capture->GetBuffer(&data, &frames, &flags, nullptr, &qpcPosition);
            if (FAILED(bufferHr)) {
                fail("GetBuffer", bufferHr);
                return;
            }
            drained = true;
            const std::size_t packetBytes = static_cast<std::size_t>(frames) * blockAlign;
            const std::uint64_t nowUs = MonotonicUs(clock_);
            const std::uint64_t durationUs =
                static_cast<std::uint64_t>(frames) * kUsPerSecond / rate;
            const std::uint64_t qpcUs =
                qpcPosition != 0 ? qpcOffsetUs + QpcTicksToUs(qpcPosition) : 0;

            std::uint64_t timestampUs = 0;
            if (!anchored) {
                timestampUs = qpcUs != 0 ? qpcUs : nowUs - durationUs;
                anchored = true;
            } else if (qpcUs != 0 && lastQpcUs != 0 &&
                       qpcUs > lastQpcUs + lastDurationUs + 5'000) {
                timestampUs = qpcUs; // real gap: trust the engine's position
            } else {
                timestampUs = nextStartUs; // contiguous: ignore stamp jitter
            }
            if (qpcUs != 0) lastQpcUs = qpcUs;

            const std::int16_t* samples = nullptr;
            const BYTE* raw = data;
            if (data != nullptr && frames > 0 && !(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                samples = reinterpret_cast<const std::int16_t*>(data);
            } else {
                silence.assign(packetBytes, 0);
                samples = reinterpret_cast<const std::int16_t*>(silence.data());
                raw = silence.data();
            }
            if (dump != nullptr && packetBytes > 0) fwrite(raw, 1, packetBytes, dump);
            timeline.Push(timestampUs, samples, frames);
            nextStartUs = timestampUs + durationUs;
            lastDurationUs = durationUs;
            pushedFrames += frames;
            checkDelivery(nowUs, frames);
            capture->ReleaseBuffer(frames);
        }
        if (!drained) {
            // Endpoint idle: poll gently (well under the 2 s WASAPI buffer).
            Sleep(3);
        }
    }

    if (dump != nullptr) fclose(dump);
    client->Stop();
    cleanup();
}

// Mixes both timelines on a fixed wall-clock grid into one continuous stream.
// Grid-driven (not source-driven) so a silent loopback does not drop
// mic-only passages, and a silent mic does not drop system audio.
void AudioCapture::MixerLoop() {
    const std::uint32_t rate = format_.sampleRate;
    const std::uint32_t channels = format_.channels;
    constexpr std::uint64_t kStepUs = 10'000; // 10 ms mixing grid
    constexpr int kStepsPerChunk = 4;         // ~40 ms output chunks

    FILE* dump = nullptr;
    {
        wchar_t path[MAX_PATH] = {};
        if (GetEnvironmentVariableW(kDumpEnv, path, MAX_PATH) > 0 && path[0] != 0) {
            _wfopen_s(&dump, path, L"wb");
        }
    }

    const std::size_t stepCap = rate / 100 + 2;
    std::vector<std::int16_t> loopbackScratch(stepCap * channels, 0);
    std::vector<std::int16_t> micScratch(stepCap * channels, 0);
    std::vector<std::int16_t> mixBuf;
    mixBuf.reserve(stepCap * channels * (kStepsPerChunk + 1));

    bool anchored = false;
    std::uint64_t cursorUs = 0;
    std::uint64_t chunkStartUs = 0;
    std::size_t chunkFrames = 0;
    int stepsInChunk = 0;
    std::uint64_t remainder = 0; // rate*us accumulator: exact frames per step
    using Clock = std::chrono::steady_clock;
    Clock::time_point nextTick{};

    const float gain = options_.micGain;
    const bool hasMic = options_.microphone;

    auto flushChunk = [&]() {
        if (chunkFrames == 0) return;
        if (dump != nullptr) {
            fwrite(mixBuf.data(), 1, mixBuf.size() * sizeof(std::int16_t), dump);
        }
        AudioChunk chunk;
        chunk.pcm.assign(mixBuf.begin(), mixBuf.end());
        chunk.frames = chunkFrames;
        chunk.timestampUs = chunkStartUs;
        mixBuf.clear();
        chunkFrames = 0;
        stepsInChunk = 0;
        if (sink_) sink_(std::move(chunk));
    };

    auto processStep = [&](std::uint64_t stepUs) {
        remainder += static_cast<std::uint64_t>(rate) * stepUs;
        const std::size_t stepFrames = static_cast<std::size_t>(remainder / kUsPerSecond);
        remainder %= kUsPerSecond;
        if (stepFrames == 0) {
            cursorUs += stepUs;
            return;
        }

        auto pull = [&](TimelineBuffer& buf, std::vector<std::int16_t>& scratch) {
            std::fill(scratch.begin(),
                      scratch.begin() +
                          static_cast<std::ptrdiff_t>(stepFrames * channels),
                      static_cast<std::int16_t>(0));
            if (buf.Empty()) return;
            buf.DropBefore(cursorUs);
            std::size_t filled = 0;
            if (!buf.Empty() && buf.StartUs() > cursorUs) {
                // Source data starts inside this window: silence lead-in.
                std::uint64_t lead = buf.UsToFrames(buf.StartUs() - cursorUs);
                if (lead > stepFrames) lead = stepFrames;
                filled = static_cast<std::size_t>(lead);
            }
            if (filled < stepFrames) {
                filled += buf.Consume(scratch.data() + filled * channels, stepFrames - filled);
            }
        };

        pull(*loopbackBuf_, loopbackScratch);
        if (hasMic) pull(*micBuf_, micScratch);

        const std::size_t n = stepFrames * channels;
        mixBuf.resize((chunkFrames + stepFrames) * channels);
        for (std::size_t i = 0; i < n; ++i) {
            std::int32_t mixed = loopbackScratch[i];
            if (hasMic) {
                mixed += static_cast<std::int32_t>(std::lround(micScratch[i] * gain));
                if (mixed > 32767) mixed = 32767;
                else if (mixed < -32768) mixed = -32768;
            }
            mixBuf[chunkFrames * channels + i] = static_cast<std::int16_t>(mixed);
        }
        if (chunkFrames == 0) chunkStartUs = cursorUs;
        chunkFrames += stepFrames;
        cursorUs += stepUs;
        if (++stepsInChunk >= kStepsPerChunk) flushChunk();
    };

    while (!stopped_.load(std::memory_order_acquire)) {
        if (!anchored) {
            // Anchor the output timeline on the first arriving source data.
            std::uint64_t earliest = UINT64_MAX;
            for (const TimelineBuffer* buf : {loopbackBuf_.get(), micBuf_.get()}) {
                if (buf != nullptr && !buf->Empty()) earliest = std::min(earliest, buf->StartUs());
            }
            if (earliest == UINT64_MAX) {
                Sleep(4);
                continue;
            }
            cursorUs = earliest;
            anchored = true;
            nextTick = Clock::now() + std::chrono::microseconds(kStepUs);
        }
        Sleep(4);
        int budget = 0;
        while (Clock::now() >= nextTick && budget++ < 20) {
            nextTick += std::chrono::microseconds(kStepUs);
            processStep(kStepUs);
        }
        if (budget >= 20) {
            // This thread was starved: resync the schedule instead of
            // spiraling through a growing backlog.
            nextTick = Clock::now() + std::chrono::microseconds(kStepUs);
        }
    }

    // Flush the tail (bounded) so the recording end keeps both sources.
    for (int i = 0; i < 60; ++i) {
        const bool loopbackEmpty = loopbackBuf_ == nullptr || loopbackBuf_->Empty();
        const bool micEmpty = micBuf_ == nullptr || micBuf_->Empty();
        if (loopbackEmpty && micEmpty) break;
        processStep(kStepUs);
        Sleep(2);
    }
    flushChunk();
    if (dump != nullptr) fclose(dump);

    const std::uint64_t loopbackDropped = loopbackBuf_ ? loopbackBuf_->DroppedFrames() : 0;
    const std::uint64_t micDropped = micBuf_ ? micBuf_->DroppedFrames() : 0;
    if (loopbackDropped > 0 || micDropped > 0) {
        log::Warning("Encoder", "AUDIO_MIX_DROPPED_SAMPLES", L"",
                     L"Timeline overflow dropped audio frames (mixer fell behind).",
                     {{"loopback", std::to_string(loopbackDropped)},
                      {"mic", std::to_string(micDropped)}});
    }
}

} // namespace vsr::encoder
