// MeetRec - offline transcode queue (ADR-007).
//
// One worker thread owns a FIFO queue: it probes newly added files (even
// before Start) and then runs them one at a time with ffmpeg. The UI polls
// Snapshot(); no UI callback crosses threads, which keeps window teardown
// simple. Suspend() requeues the running job and stops starting new ones -
// the recorder calls it while capturing so x264 never competes with itself.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Probe.h"
#include "TranscodePreset.h"

namespace vsr::transcode {

enum class JobState { Queued, Probing, Running, Completed, Failed };

const char* ToString(JobState state) noexcept;

struct JobSnapshot {
    std::uint64_t id = 0;
    std::wstring inputPath;
    std::wstring outputPath;     // final path; may not exist until completion
    JobState state = JobState::Queued;
    double progress = -1.0;      // 0..1, <0 when the duration is unknown
    std::int64_t processedMs = 0;
    std::int64_t durationMs = 0;
    double speed = 0.0;
    std::uint64_t outputBytes = 0;
    std::uint64_t sourceBytes = 0;
    std::uint64_t estimatedBytes = 0;
    MediaInfo media;
    bool probed = false;
    bool probeFailed = false;
    bool sourceAlreadySmall = false;
    std::wstring error;          // technical detail for a failed job
    std::wstring note;           // advisory, e.g. low-bitrate source
};

struct ServiceSnapshot {
    std::vector<JobSnapshot> jobs;
    bool active = false;         // Start() was called and the queue is live
    bool suspended = false;      // recording in progress
    bool running = false;        // a job is executing right now
    std::wstring ffmpegPath;
    std::wstring ffprobePath;
    std::wstring toolError;      // non-empty when a required tool is missing
};

class TranscodeService {
public:
    TranscodeService();
    ~TranscodeService();

    TranscodeService(const TranscodeService&) = delete;
    TranscodeService& operator=(const TranscodeService&) = delete;

    // Returns the new job id, or 0 when the file is already queued.
    std::uint64_t Add(const std::filesystem::path& input,
                      const std::filesystem::path& outputDir);
    // Ignores the running job; the UI only enables removal for idle rows.
    void Remove(std::uint64_t id);
    void ClearFinished();

    // Starts (or resumes) processing with the given preset. Failed jobs are
    // retried by the next Start.
    void Start(Quality quality, Resolution resolution);
    // Updates the preset used for estimates without starting the queue.
    void SetPreset(Quality quality, Resolution resolution);
    // Re-targets not-yet-started jobs (empty = same folder as each source).
    void SetOutputDirectory(const std::filesystem::path& outputDir);
    // Stops after the current job; that job returns to the queue.
    void CancelCurrent();
    // Requeues the running job and blocks new ones until Resume().
    void Suspend();
    void Resume();

    ServiceSnapshot Snapshot() const;
    void Shutdown();

private:
    struct Job {
        std::uint64_t id = 0;
        std::filesystem::path input;
        std::filesystem::path outputDir;
        std::filesystem::path output;
        std::filesystem::path partial;
        JobState state = JobState::Queued;
        bool probed = false;
        bool probeFailed = false;
        MediaInfo media;
        double progress = -1.0;
        std::int64_t processedMs = 0;
        std::int64_t durationMs = 0;
        double speed = 0.0;
        std::uint64_t outputBytes = 0;
        std::uint64_t estimatedBytes = 0;
        std::uint64_t sourceBytes = 0;
        bool sourceAlreadySmall = false;
        std::wstring error;
    };

    void WorkerMain();
    bool HasUnprobedLocked() const;
    bool HasRunnableLocked() const;
    std::uint64_t PickLocked() const;

    void ProbeJob(std::uint64_t id);
    void RunJob(std::uint64_t id);
    void RequeueAfterCancel(std::uint64_t id);
    void RecomputeEstimatesLocked();
    Job* FindLocked(std::uint64_t id);

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<Job> jobs_;
    std::uint64_t nextId_ = 1;
    Quality quality_ = Quality::Standard;
    Resolution resolution_ = Resolution::Keep;
    bool active_ = false;
    bool suspended_ = false;
    bool stop_ = false;
    std::atomic<bool> cancelCurrent_{false};
    std::wstring ffmpegPath_;
    std::wstring ffprobePath_;
    std::wstring toolError_;
    std::thread worker_;
};

} // namespace vsr::transcode
