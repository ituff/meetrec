#include "SpeechSession.h"

#include <windows.h>

#include <Infrastructure/Log.h>
#include <Infrastructure/Strings.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "SenseVoiceRecognizer.h"
#include "SpeechResampler.h"
#include "SpeechVad.h"
#include "SubtitleFormat.h"

namespace vsr::speech {

namespace {

constexpr int kOutputSampleRate = 16000;
constexpr std::size_t kMaxQueuedInputs = 1200; // ~2 min of 200 ms chunks
constexpr int kGapToleranceUs = 100'000;

std::int64_t SamplesToUs(std::int64_t samples) {
    return samples * 1'000'000 / kOutputSampleRate;
}

} // namespace

struct SpeechSession::Impl {
    struct Input {
        enum class Kind { Audio, Pause, Resume };
        Kind kind = Kind::Audio;
        bool microphone = false;
        std::int64_t timestampUs = 0;
        std::vector<std::int16_t> samples;
    };

    struct Epoch {
        bool active = false;
        std::int64_t baseUs = 0;
        std::int64_t lastEndUs = 0;
    };

    explicit Impl(const SpeechSessionOptions& options_)
        : options(options_),
          systemResampler(options.sourceSampleRate, kOutputSampleRate),
          microphoneResampler(options.sourceSampleRate, kOutputSampleRate),
          store(options.chinese) {}

    SpeechSessionOptions options;
    SpeechResampler systemResampler;
    SpeechResampler microphoneResampler;
    TranscriptStore store;
    SenseVoiceRecognizer recognizer;
    std::unique_ptr<SpeechVad> systemVad;
    std::unique_ptr<SpeechVad> microphoneVad;

    std::thread worker;
    mutable std::mutex mutex;
    std::condition_variable cv;
    mutable std::condition_variable readyCv;
    std::deque<Input> queue;
    std::atomic<bool> stopping{false};
    std::atomic<bool> pauseRequested{false};
    std::atomic<std::size_t> segmentCount{0};
    std::uint64_t droppedChunks = 0; // guarded by mutex

    State state = State::Loading; // guarded by mutex
    std::wstring detail;          // guarded by mutex
    bool transcriptFinalized = false; // guarded by mutex

    // Worker-only state.
    bool pausedWorker = false;
    Epoch systemEpoch;
    Epoch microphoneEpoch;
    std::vector<float> resampledScratch;

    void WorkerLoop();
    bool LoadModels();
    bool Fail(const std::wstring& error);
    void ProcessAudio(const Input& input);
    void DrainSource(bool microphone);
    void FlushSource(bool microphone);
    void FlushAll();
    void StartEpoch(bool microphone, std::int64_t timestampUs);
    void Shutdown();
};

bool SpeechSession::Impl::Fail(const std::wstring& error) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        state = State::Failed;
        detail = error;
    }
    readyCv.notify_all();
    log::Warning("Speech", "SPEECH_MODEL_UNAVAILABLE", L"",
                 L"Meeting transcription is disabled for this recording; the "
                 L"recording itself continues.",
                 {{"detail", WideToUtf8(error)}});
    return false;
}

bool SpeechSession::Impl::LoadModels() {
    const auto started = std::chrono::steady_clock::now();
    std::wstring error;
    if (!SpeechRuntimeAvailable(error)) {
        return Fail(error);
    }

    systemVad = std::make_unique<SpeechVad>();
    if (!systemVad->Create(options.model.vad, error)) {
        return Fail(error);
    }
    if (options.microphone) {
        microphoneVad = std::make_unique<SpeechVad>();
        if (!microphoneVad->Create(options.model.vad, error)) {
            return Fail(error);
        }
    }
    if (!recognizer.Create(options.model.model, options.model.tokens, options.numThreads,
                           error)) {
        return Fail(error);
    }

    const bool partialOpened = store.OpenPartial(options.basePath);
    {
        std::lock_guard<std::mutex> lock(mutex);
        state = State::Ready;
        detail = partialOpened ? L"ready" : L"ready (transcript journal unavailable)";
    }
    readyCv.notify_all();

    const double loadMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
    log::Info("Speech", "SPEECH_MODEL_LOADED", L"",
              L"SenseVoiceSmall int8 is ready for meeting transcription.",
              {{"loadMs", std::to_string(static_cast<long long>(loadMs))},
               {"threads", std::to_string(options.numThreads)},
               {"microphone", options.microphone ? "1" : "0"},
               {"model", WideToUtf8(options.model.model.wstring())},
               {"partialJournal", partialOpened ? "1" : "0"}});
    return true;
}

void SpeechSession::Impl::StartEpoch(bool microphone, std::int64_t timestampUs) {
    Epoch& epoch = microphone ? microphoneEpoch : systemEpoch;
    epoch.active = true;
    epoch.baseUs = timestampUs;
    epoch.lastEndUs = timestampUs;
    (microphone ? microphoneResampler : systemResampler).Reset();
    SpeechVad& vad = microphone ? *microphoneVad : *systemVad;
    vad.Reset();
}

void SpeechSession::Impl::ProcessAudio(const Input& input) {
    if (input.microphone && microphoneVad == nullptr) return;
    if (!input.microphone && systemVad == nullptr) return;

    Epoch& epoch = input.microphone ? microphoneEpoch : systemEpoch;
    SpeechVad& vad = input.microphone ? *microphoneVad : *systemVad;
    SpeechResampler& resampler =
        input.microphone ? microphoneResampler : systemResampler;

    if (!epoch.active) {
        StartEpoch(input.microphone, input.timestampUs);
    } else if (input.timestampUs > epoch.lastEndUs + kGapToleranceUs ||
               input.timestampUs + kGapToleranceUs < epoch.lastEndUs) {
        // The mixer cannot produce a hole on its own; a jump means queued
        // audio was dropped (CPU stall) or the source was restarted. Flush the
        // current utterance and start a new VAD epoch so timestamps stay
        // anchored to the recording timeline.
        const std::int64_t gapUs = input.timestampUs - epoch.lastEndUs;
        FlushSource(input.microphone);
        StartEpoch(input.microphone, input.timestampUs);
        log::Warning("Speech", "SPEECH_AUDIO_GAP", L"",
                     L"A gap in the speech tap reset the VAD epoch.",
                     {{"source", input.microphone ? "mic" : "loopback"},
                      {"gapUs", std::to_string(gapUs)}});
    }

    resampledScratch.clear();
    resampler.Process(input.samples.data(), input.samples.size(), resampledScratch);

    for (std::size_t offset = 0; offset < resampledScratch.size();) {
        const std::size_t remaining = resampledScratch.size() - offset;
        const int count = static_cast<int>(std::min<std::size_t>(512, remaining));
        vad.Accept(resampledScratch.data() + offset, count);
        offset += static_cast<std::size_t>(count);
    }
    DrainSource(input.microphone);

    const std::int64_t durationUs =
        static_cast<std::int64_t>(input.samples.size()) * 1'000'000 /
        static_cast<std::int64_t>(options.sourceSampleRate);
    epoch.lastEndUs = input.timestampUs + durationUs;
}

void SpeechSession::Impl::DrainSource(bool microphone) {
    SpeechVad& vad = microphone ? *microphoneVad : *systemVad;
    Epoch& epoch = microphone ? microphoneEpoch : systemEpoch;

    VadSpeechSegment segment;
    while (vad.Pop(segment)) {
        if (segment.startSample < 0 || segment.samples.empty()) continue;

        const std::int64_t startUs = epoch.baseUs + SamplesToUs(segment.startSample);
        const std::int64_t endUs =
            startUs + SamplesToUs(static_cast<std::int64_t>(segment.samples.size()));

        RecognizedSpeech recognized;
        std::wstring error;
        const auto decodeStart = std::chrono::steady_clock::now();
        if (!recognizer.Recognize(segment.samples.data(),
                                  static_cast<int>(segment.samples.size()), recognized,
                                  error)) {
            log::Warning("Speech", "SPEECH_DECODE_FAILED", L"",
                         L"A speech segment could not be decoded and was skipped.",
                         {{"detail", WideToUtf8(error)}});
            continue;
        }
        const double decodeMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                      decodeStart)
                .count();

        std::wstring text = SanitizeTranscriptText(recognized.text);
        if (text.empty()) continue;

        TranscriptSegment transcript;
        transcript.startUs = std::max<std::int64_t>(0, startUs);
        transcript.endUs = std::max<std::int64_t>(transcript.startUs + 1'000, endUs);
        transcript.microphone = microphone;
        transcript.language = recognized.language;
        transcript.text = std::move(text);
        store.Add(std::move(transcript));
        segmentCount.fetch_add(1, std::memory_order_relaxed);

        if (decodeMs > 2000.0) {
            log::Debug("Speech", "SPEECH_DECODE_SLOW", L"",
                       L"A speech segment took unusually long to decode.",
                       {{"decodeMs", std::to_string(static_cast<long long>(decodeMs))},
                        {"seconds",
                         std::to_string(static_cast<double>(segment.samples.size()) /
                                        kOutputSampleRate)},
                        {"source", microphone ? "mic" : "loopback"}});
        }
    }
}

void SpeechSession::Impl::FlushSource(bool microphone) {
    if (microphone ? microphoneVad == nullptr : systemVad == nullptr) return;
    SpeechVad& vad = microphone ? *microphoneVad : *systemVad;
    Epoch& epoch = microphone ? microphoneEpoch : systemEpoch;
    if (!epoch.active) return;
    vad.Flush();
    DrainSource(microphone);
    vad.Reset();
    epoch = Epoch{};
    (microphone ? microphoneResampler : systemResampler).Reset();
}

void SpeechSession::Impl::FlushAll() {
    FlushSource(false);
    if (microphoneVad != nullptr) FlushSource(true);
}

void SpeechSession::Impl::WorkerLoop() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    if (!LoadModels()) return;

    for (;;) {
        Input input;
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [this] {
                return stopping.load(std::memory_order_acquire) || !queue.empty();
            });
            if (queue.empty()) {
                if (stopping.load(std::memory_order_acquire)) break;
                continue;
            }
            input = std::move(queue.front());
            queue.pop_front();
        }

        if (stopping.load(std::memory_order_acquire) && input.kind != Input::Kind::Audio) {
            continue; // controls after stop are meaningless
        }
        switch (input.kind) {
            case Input::Kind::Audio:
                if (!pausedWorker) ProcessAudio(input);
                break;
            case Input::Kind::Pause:
                pausedWorker = true;
                FlushAll();
                break;
            case Input::Kind::Resume:
                pausedWorker = false;
                break;
        }
    }

    pausedWorker = false;
    FlushAll();
    {
        std::lock_guard<std::mutex> lock(mutex);
        state = State::Stopped;
        detail = L"stopped";
    }
    readyCv.notify_all();
}

void SpeechSession::Impl::Shutdown() {
    stopping.store(true, std::memory_order_release);
    cv.notify_all();
    if (worker.joinable()) worker.join();
}

SpeechSession::SpeechSession(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

SpeechSession::~SpeechSession() {
    if (impl_) impl_->Shutdown();
}

Result<std::unique_ptr<SpeechSession>> SpeechSession::Start(
    const SpeechSessionOptions& options) {
    if (options.basePath.empty()) {
        return Error(ErrorCode::InternalState,
                     L"Meeting transcription could not start.",
                     L"SpeechSession::Start: empty transcript base path");
    }
    if (options.sourceSampleRate < 8000 || options.sourceSampleRate > 192000) {
        return Error(ErrorCode::InternalState,
                     L"Meeting transcription could not start.",
                     L"SpeechSession::Start: unsupported source sample rate " +
                         std::to_wstring(options.sourceSampleRate));
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(options.model.model, ec) || ec) {
        return Error(ErrorCode::InternalState,
                     L"Meeting transcription could not start.",
                     L"SpeechSession::Start: missing model " +
                         options.model.model.wstring());
    }

    auto impl = std::make_unique<Impl>(options);
    impl->worker = std::thread([raw = impl.get()] { raw->WorkerLoop(); });
    return std::unique_ptr<SpeechSession>(new SpeechSession(std::move(impl)));
}

void SpeechSession::PushAudio(bool microphone, const std::int16_t* mono,
                              std::size_t frames, std::int64_t outputTimestampUs) {
    if (impl_ == nullptr || mono == nullptr || frames == 0) return;
    if (impl_->stopping.load(std::memory_order_acquire)) return;

    Impl::Input input;
    input.kind = Impl::Input::Kind::Audio;
    input.microphone = microphone;
    input.timestampUs = outputTimestampUs;
    input.samples.assign(mono, mono + frames);

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->queue.size() >= kMaxQueuedInputs) {
        ++impl_->droppedChunks;
        return;
    }
    impl_->queue.push_back(std::move(input));
    impl_->cv.notify_one();
}

void SpeechSession::SetPaused(bool paused) {
    if (impl_ == nullptr) return;
    const bool previous = impl_->pauseRequested.exchange(paused, std::memory_order_acq_rel);
    if (previous == paused) return;
    if (impl_->stopping.load(std::memory_order_acquire)) return;

    Impl::Input input;
    input.kind = paused ? Impl::Input::Kind::Pause : Impl::Input::Kind::Resume;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->queue.push_back(std::move(input));
    }
    impl_->cv.notify_one();
}

bool SpeechSession::WaitForReady(int timeoutMs) const {
    if (impl_ == nullptr) return false;
    std::unique_lock<std::mutex> lock(impl_->mutex);
    impl_->readyCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] {
        return impl_->state != State::Loading;
    });
    return impl_->state == State::Ready;
}

void SpeechSession::Stop() {
    if (impl_ != nullptr) impl_->Shutdown();
}

TranscriptStore::Written SpeechSession::Finalize() {
    if (impl_ == nullptr) return {};
    impl_->Shutdown();
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->transcriptFinalized) return {};
        impl_->transcriptFinalized = true;
    }
    const auto started = std::chrono::steady_clock::now();
    TranscriptStore::Written written = impl_->store.Finalize(impl_->options.basePath);
    const double finalizeMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
            .count();
    log::Info("Speech", "SPEECH_TRANSCRIPT_FINALIZED", L"",
              L"Meeting transcript written.",
              {{"segments", std::to_string(impl_->segmentCount.load())},
               {"srt", WideToUtf8(written.srt.wstring())},
               {"txt", WideToUtf8(written.txt.wstring())},
               {"droppedChunks", std::to_string(impl_->droppedChunks)},
               {"finalizeMs", std::to_string(static_cast<long long>(finalizeMs))}});
    return written;
}

SpeechSession::Status SpeechSession::GetStatus() const {
    Status status;
    if (impl_ == nullptr) return status;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    status.state = impl_->state;
    status.segments = impl_->segmentCount.load(std::memory_order_relaxed);
    status.detail = impl_->detail;
    return status;
}

} // namespace vsr::speech
