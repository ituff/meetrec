// MeetRec - asynchronous live caption translation.
//
// Polls the CaptionFeed from the UI timer, translates new cues on a worker
// thread through Bing/Edge, and writes the result back into the feed. One
// request at a time keeps rate-limit pressure low; failures leave the original
// caption visible.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include <Speech/CaptionFeed.h>

namespace vsr::app {

class CaptionTranslator {
public:
    explicit CaptionTranslator(std::shared_ptr<speech::CaptionFeed> feed);
    ~CaptionTranslator();

    CaptionTranslator(const CaptionTranslator&) = delete;
    CaptionTranslator& operator=(const CaptionTranslator&) = delete;

    // Changes the target language and clears previous translations so the
    // visible cues are re-translated.
    void SetTargetLanguage(const std::string& language);

    // Called from the UI thread (200 ms timer). `enabled` gates network use;
    // when false, pending jobs are dropped and the worker stops applying
    // results.
    void Poll(bool enabled);

    void Stop();

private:
    struct Job {
        std::uint64_t sequence = 0;
        std::wstring text;
        std::string target;
    };

    void WorkerLoop();

    std::shared_ptr<speech::CaptionFeed> feed_;
    std::thread worker_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Job> queue_;
    std::set<std::uint64_t> inFlight_;
    std::string target_ = "en";
    bool enabled_ = false;
    bool stopping_ = false;
};

} // namespace vsr::app
