#include "TranscodeService.h"

#include <algorithm>
#include <chrono>
#include <cwctype>

#include <Infrastructure/Log.h>
#include <Infrastructure/Strings.h>

#include "Arguments.h"
#include "ChildProcess.h"
#include "OutputNaming.h"
#include "ProgressParser.h"

namespace vsr::transcode {

const char* ToString(JobState state) noexcept {
    switch (state) {
        case JobState::Queued: return "queued";
        case JobState::Probing: return "probing";
        case JobState::Running: return "running";
        case JobState::Completed: return "completed";
        case JobState::Failed: return "failed";
    }
    return "unknown";
}

namespace {

std::wstring TrimDetail(const std::string& stderrTail) {
    std::wstring text = Utf8ToWide(stderrTail);
    while (!text.empty() && (iswspace(text.back()) || text.back() == L'\r')) text.pop_back();
    const std::size_t newline = text.find_last_of(L'\n');
    if (newline != std::wstring::npos && newline + 1 < text.size()) {
        text = text.substr(newline + 1);
    }
    if (text.size() > 240) text = text.substr(0, 240) + L"...";
    return text;
}

double ProgressFraction(std::int64_t processedMs, std::int64_t durationMs) {
    if (durationMs <= 0 || processedMs <= 0) return -1.0;
    return std::min(1.0, static_cast<double>(processedMs) / static_cast<double>(durationMs));
}

} // namespace

TranscodeService::TranscodeService() {
    auto ffmpeg = LocateFfmpeg();
    if (ffmpeg) {
        ffmpegPath_ = ffmpeg.Value();
    } else {
        toolError_ = ffmpeg.GetError().userMessage;
    }
    auto ffprobe = LocateFfprobe();
    if (ffprobe) ffprobePath_ = ffprobe.Value();
    worker_ = std::thread(&TranscodeService::WorkerMain, this);
}

TranscodeService::~TranscodeService() {
    Shutdown();
}

void TranscodeService::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) return;
        stop_ = true;
        cancelCurrent_.store(true);
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

std::uint64_t TranscodeService::Add(const std::filesystem::path& input,
                                    const std::filesystem::path& outputDir) {
    if (input.empty()) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const Job& job : jobs_) {
        if (EqualsNoCase(job.input.wstring(), input.wstring())) return 0;
    }
    Job job;
    job.id = nextId_++;
    job.input = input;
    job.outputDir = outputDir;
    job.output = EnsureUniquePath(DeriveOutputPath(input, outputDir));
    job.partial = PartialPathFor(job.output);
    job.sourceBytes = FileSizeBytes(input);
    job.estimatedBytes = 0;
    jobs_.push_back(std::move(job));
    cv_.notify_all();
    return jobs_.back().id;
}

void TranscodeService::Remove(std::uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
                               [id](const Job& job) {
                                   return job.id == id && job.state != JobState::Running &&
                                          job.state != JobState::Probing;
                               }),
                jobs_.end());
}

void TranscodeService::ClearFinished() {
    std::lock_guard<std::mutex> lock(mutex_);
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
                               [](const Job& job) {
                                   return job.state == JobState::Completed ||
                                          job.state == JobState::Failed;
                               }),
                jobs_.end());
}

void TranscodeService::Start(Quality quality, Resolution resolution) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quality_ = quality;
        resolution_ = resolution;
        active_ = true;
        cancelCurrent_.store(false);
        for (Job& job : jobs_) {
            if (job.state == JobState::Failed) {
                job.state = JobState::Queued;
                job.error.clear();
            }
        }
        RecomputeEstimatesLocked();
    }
    cv_.notify_all();
}

void TranscodeService::SetPreset(Quality quality, Resolution resolution) {
    std::lock_guard<std::mutex> lock(mutex_);
    quality_ = quality;
    resolution_ = resolution;
    RecomputeEstimatesLocked();
}

void TranscodeService::SetOutputDirectory(const std::filesystem::path& outputDir) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Job& job : jobs_) {
        if (job.state == JobState::Completed || job.state == JobState::Running ||
            job.state == JobState::Probing) {
            continue;
        }
        job.outputDir = outputDir;
        job.output = EnsureUniquePath(DeriveOutputPath(job.input, outputDir));
        job.partial = PartialPathFor(job.output);
    }
}

void TranscodeService::CancelCurrent() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = false;
        cancelCurrent_.store(true);
    }
    cv_.notify_all();
}

void TranscodeService::Suspend() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        suspended_ = true;
        cancelCurrent_.store(true);
    }
    cv_.notify_all();
}

void TranscodeService::Resume() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        suspended_ = false;
    }
    cv_.notify_all();
}

ServiceSnapshot TranscodeService::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    ServiceSnapshot snapshot;
    snapshot.active = active_;
    snapshot.suspended = suspended_;
    snapshot.ffmpegPath = ffmpegPath_;
    snapshot.ffprobePath = ffprobePath_;
    snapshot.toolError = toolError_;
    snapshot.jobs.reserve(jobs_.size());
    for (const Job& job : jobs_) {
        JobSnapshot out;
        out.id = job.id;
        out.inputPath = job.input.wstring();
        out.outputPath = job.output.wstring();
        out.state = job.state;
        out.progress = job.progress;
        out.processedMs = job.processedMs;
        out.durationMs = job.durationMs;
        out.speed = job.speed;
        out.outputBytes = job.outputBytes;
        out.sourceBytes = job.sourceBytes;
        out.estimatedBytes = job.estimatedBytes;
        out.media = job.media;
        out.probed = job.probed;
        out.probeFailed = job.probeFailed;
        out.sourceAlreadySmall = job.sourceAlreadySmall;
        out.error = job.error;
        if (job.state == JobState::Running || job.state == JobState::Probing) {
            snapshot.running = true;
        }
        if (job.probed && !job.probeFailed) {
            const EncodeOptions options = OptionsForQuality(quality_);
            out.estimatedBytes =
                EstimatedOutputBytes(options, job.media.durationMs);
            out.sourceAlreadySmall = job.media.bitrateBps > 0 &&
                                     job.media.bitrateBps <
                                         static_cast<std::int64_t>(options.maxrateBps / 2);
        }
        snapshot.jobs.push_back(std::move(out));
    }
    return snapshot;
}

bool TranscodeService::HasUnprobedLocked() const {
    return std::any_of(jobs_.begin(), jobs_.end(), [](const Job& job) {
        return job.state == JobState::Queued && !job.probed && !job.probeFailed;
    });
}

bool TranscodeService::HasRunnableLocked() const {
    return std::any_of(jobs_.begin(), jobs_.end(), [](const Job& job) {
        return job.state == JobState::Queued && job.probed;
    });
}

std::uint64_t TranscodeService::PickLocked() const {
    // Probe first so every row shows duration/estimate before encoding starts.
    for (const Job& job : jobs_) {
        if (job.state == JobState::Queued && !job.probed && !job.probeFailed) {
            return job.id;
        }
    }
    if (!active_) return 0;
    for (const Job& job : jobs_) {
        if (job.state == JobState::Queued && job.probed) {
            return job.id;
        }
    }
    return 0;
}

TranscodeService::Job* TranscodeService::FindLocked(std::uint64_t id) {
    for (Job& job : jobs_) {
        if (job.id == id) return &job;
    }
    return nullptr;
}

void TranscodeService::RecomputeEstimatesLocked() {
    const EncodeOptions options = OptionsForQuality(quality_);
    for (Job& job : jobs_) {
        if (!job.probed || job.probeFailed) continue;
        job.estimatedBytes = EstimatedOutputBytes(options, job.media.durationMs);
        job.sourceAlreadySmall =
            job.media.bitrateBps > 0 &&
            job.media.bitrateBps < static_cast<std::int64_t>(options.maxrateBps / 2);
    }
}

void TranscodeService::WorkerMain() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        cv_.wait(lock, [this] {
            return stop_ || (!suspended_ &&
                             (HasUnprobedLocked() || (active_ && HasRunnableLocked())));
        });
        if (stop_) break;
        const std::uint64_t id = PickLocked();
        if (id == 0) continue;

        const bool probe = [this, id] {
            const Job* job = nullptr;
            for (const Job& candidate : jobs_) {
                if (candidate.id == id) job = &candidate;
            }
            return job != nullptr && !job->probed;
        }();
        if (probe) {
            if (Job* job = FindLocked(id)) job->state = JobState::Probing;
            lock.unlock();
            ProbeJob(id);
            lock.lock();
            continue;
        }

        cancelCurrent_.store(false);
        if (Job* job = FindLocked(id)) {
            job->state = JobState::Running;
            job->progress = 0.0;
            job->processedMs = 0;
            job->outputBytes = 0;
            job->speed = 0.0;
            job->error.clear();
        }
        lock.unlock();
        RunJob(id);
        lock.lock();
    }
}

void TranscodeService::ProbeJob(std::uint64_t id) {
    std::filesystem::path input;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const Job* job = FindLocked(id);
        if (job == nullptr) return;
        input = job->input;
    }

    std::wstring ffprobe = ffprobePath_;
    if (ffprobe.empty()) {
        auto located = LocateFfprobe();
        if (located) {
            ffprobe = located.Value();
            std::lock_guard<std::mutex> lock(mutex_);
            ffprobePath_ = ffprobe;
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            if (Job* job = FindLocked(id)) {
                job->probed = true;
                job->probeFailed = true;
                job->state = JobState::Queued;
                job->error = located.GetError().userMessage;
            }
            return;
        }
    }

    auto info = ProbeFile(ffprobe, input);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Job* job = FindLocked(id);
        if (job == nullptr) return;
        job->probed = true;
        job->state = JobState::Queued;
        if (info) {
            job->media = info.Value();
            job->durationMs = info.Value().durationMs;
            job->sourceBytes = info.Value().fileBytes;
            job->probeFailed = false;
            job->error.clear();
        } else {
            job->probeFailed = true;
            job->error = info.GetError().userMessage;
        }
    }
    if (info) {
        log::Info("Transcode", "PROBE_OK", L"", input.filename().wstring(),
                  {{"durationMs", std::to_string(info.Value().durationMs)},
                   {"bitrateBps", std::to_string(info.Value().bitrateBps)}});
    } else {
        log::Warning("Transcode", "PROBE_FAILED", L"", input.filename().wstring(),
                     {{"detail", WideToUtf8(info.GetError().technicalMessage)}});
    }
}

void TranscodeService::RunJob(std::uint64_t id) {
    std::filesystem::path input;
    std::filesystem::path output;
    std::filesystem::path partial;
    MediaInfo media;
    EncodeOptions options;
    Resolution resolution = Resolution::Keep;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const Job* job = FindLocked(id);
        if (job == nullptr) return;
        input = job->input;
        output = job->output;
        partial = job->partial;
        media = job->media;
        options = OptionsForQuality(quality_);
        resolution = resolution_;
        std::error_code existsEc;
        if (std::filesystem::exists(output, existsEc) && !existsEc) {
            Job* mutableJob = FindLocked(id);
            mutableJob->output = EnsureUniquePath(output);
            mutableJob->partial = PartialPathFor(mutableJob->output);
            output = mutableJob->output;
            partial = mutableJob->partial;
        }
    }

    std::wstring ffmpeg = ffmpegPath_;
    if (ffmpeg.empty()) {
        auto located = LocateFfmpeg();
        if (!located) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (Job* job = FindLocked(id)) {
                job->state = JobState::Failed;
                job->error = located.GetError().userMessage;
            }
            return;
        }
        ffmpeg = located.Value();
        std::lock_guard<std::mutex> lock(mutex_);
        ffmpegPath_ = ffmpeg;
    }

    const std::wstring arguments =
        BuildTranscodeArguments(input.wstring(), partial.wstring(), options, resolution,
                                media.width, media.height, media.frameRate);
    log::Info("Transcode", "JOB_STARTED", L"", input.filename().wstring(),
              {{"output", WideToUtf8(output.filename().wstring())}});

    StreamOptions stream;
    stream.cancel = &cancelCurrent_;
    stream.onStdoutLine = [this, id](const std::string& line) {
        const ProgressSample sample = ParseProgressLine(line);
        if (!sample.valid) return;
        std::lock_guard<std::mutex> lock(mutex_);
        Job* job = FindLocked(id);
        if (job == nullptr) return;
        if (sample.outTimeUs > 0) {
            job->processedMs = sample.outTimeUs / 1000;
            job->progress = ProgressFraction(job->processedMs, job->durationMs);
        }
        if (sample.speed > 0.0) job->speed = sample.speed;
        if (sample.totalBytes > 0) job->outputBytes = sample.totalBytes;
    };

    std::string stderrTail;
    bool timedOut = false;
    auto exit = RunChildStream(ffmpeg, arguments, stream, stderrTail, timedOut);

    if (stop_) {
        std::error_code ec;
        std::filesystem::remove(partial, ec);
        std::lock_guard<std::mutex> lock(mutex_);
        if (Job* job = FindLocked(id)) job->state = JobState::Queued;
        return;
    }
    if (!exit) {
        std::error_code ec;
        std::filesystem::remove(partial, ec);
        std::lock_guard<std::mutex> lock(mutex_);
        if (Job* job = FindLocked(id)) {
            job->state = JobState::Failed;
            job->error = exit.GetError().userMessage;
        }
        return;
    }
    if (cancelCurrent_.load()) {
        RequeueAfterCancel(id);
        return;
    }
    if (exit.Value() != 0 || timedOut) {
        std::error_code ec;
        std::filesystem::remove(partial, ec);
        const std::wstring detail = TrimDetail(stderrTail);
        std::lock_guard<std::mutex> lock(mutex_);
        if (Job* job = FindLocked(id)) {
            job->state = JobState::Failed;
            job->error = detail.empty()
                             ? L"ffmpeg exited with code " +
                                   std::to_wstring(exit.Value())
                             : detail;
        }
        log::Warning("Transcode", "JOB_FAILED", L"", input.filename().wstring(),
                     {{"exit", std::to_string(exit.Value())},
                      {"detail", WideToUtf8(detail)}});
        return;
    }

    // A successful ffmpeg exit still needs a non-empty file on disk.
    std::error_code sizeEc;
    const std::uintmax_t finalSize = std::filesystem::file_size(partial, sizeEc);
    if (sizeEc || finalSize == 0) {
        std::error_code ec;
        std::filesystem::remove(partial, ec);
        std::lock_guard<std::mutex> lock(mutex_);
        if (Job* job = FindLocked(id)) {
            job->state = JobState::Failed;
            job->error = L"The encoder produced no output file.";
        }
        return;
    }

    bool renamed = MoveFileExW(partial.c_str(), output.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
    DWORD moveError = renamed ? ERROR_SUCCESS : GetLastError();
    if (!renamed) {
        const std::filesystem::path alternative = EnsureUniquePath(output);
        renamed = MoveFileExW(partial.c_str(), alternative.c_str(),
                              MOVEFILE_WRITE_THROUGH) != FALSE;
        if (renamed) output = alternative;
        else moveError = GetLastError();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Job* job = FindLocked(id);
        if (job == nullptr) return;
        if (!renamed) {
            std::error_code ec;
            std::filesystem::remove(partial, ec);
            job->state = JobState::Failed;
            job->error = L"The compressed file could not be saved (error " +
                         std::to_wstring(moveError) + L").";
            return;
        }
        job->output = output;
        job->partial = PartialPathFor(output);
        job->state = JobState::Completed;
        job->progress = 1.0;
        job->processedMs = job->durationMs > 0 ? job->durationMs : job->processedMs;
        job->outputBytes = static_cast<std::uint64_t>(finalSize);
    }
    log::Info("Transcode", "JOB_COMPLETED", L"", input.filename().wstring(),
              {{"bytes", std::to_string(finalSize)}});
}

void TranscodeService::RequeueAfterCancel(std::uint64_t id) {
    std::error_code ec;
    std::filesystem::path partial;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const Job* job = FindLocked(id);
        if (job == nullptr) return;
        partial = job->partial;
    }
    std::filesystem::remove(partial, ec);
    std::lock_guard<std::mutex> lock(mutex_);
    if (Job* job = FindLocked(id)) {
        job->state = JobState::Queued;
        job->progress = -1.0;
        job->processedMs = 0;
        job->outputBytes = 0;
        job->speed = 0.0;
    }
    log::Info("Transcode", "JOB_CANCELLED", L"", L"");
}

} // namespace vsr::transcode
