#include "ProgressParser.h"

#include <cstdlib>
#include <cstring>

namespace vsr::transcode {

namespace {

std::int64_t ParseInt64(std::string_view text) {
    if (text.empty()) return 0;
    char* end = nullptr;
    const std::string copy(text);
    const long long value = std::strtoll(copy.c_str(), &end, 10);
    return end != copy.c_str() ? static_cast<std::int64_t>(value) : 0;
}

std::uint64_t ParseUInt64(std::string_view text) {
    if (text.empty()) return 0;
    char* end = nullptr;
    const std::string copy(text);
    const unsigned long long value = std::strtoull(copy.c_str(), &end, 10);
    return end != copy.c_str() ? static_cast<std::uint64_t>(value) : 0;
}

std::int64_t ParseClock(std::string_view text) {
    // HH:MM:SS.ffffff
    const std::size_t first = text.find(':');
    if (first == std::string_view::npos) return 0;
    const std::size_t second = text.find(':', first + 1);
    if (second == std::string_view::npos) return 0;
    const std::int64_t hours = ParseInt64(text.substr(0, first));
    const std::int64_t minutes = ParseInt64(text.substr(first + 1, second - first - 1));
    const std::string secondsText(text.substr(second + 1));
    char* end = nullptr;
    const double seconds = std::strtod(secondsText.c_str(), &end);
    if (end == secondsText.c_str()) return 0;
    return ((hours * 3600 + minutes * 60) * 1'000'000) +
           static_cast<std::int64_t>(seconds * 1'000'000.0);
}

double ParseSpeed(std::string_view text) {
    if (text.empty() || text == "N/A") return 0.0;
    std::string copy(text);
    if (!copy.empty() && copy.back() == 'x') copy.pop_back();
    char* end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    return end != copy.c_str() ? value : 0.0;
}

} // namespace

ProgressSample ParseProgressLine(std::string_view line) {
    ProgressSample sample;
    const std::size_t equals = line.find('=');
    if (equals == std::string_view::npos) return sample;
    const std::string_view key = line.substr(0, equals);
    const std::string_view value = line.substr(equals + 1);

    if (key == "out_time_us") {
        sample.outTimeUs = ParseInt64(value);
        sample.valid = true;
    } else if (key == "out_time_ms") {
        // Historical ffmpeg quirk: out_time_ms is microseconds, not
        // milliseconds. out_time_us wins when both are present.
        if (sample.outTimeUs == 0) sample.outTimeUs = ParseInt64(value);
        sample.valid = true;
    } else if (key == "out_time") {
        const std::int64_t clock = ParseClock(value);
        if (clock > 0 && sample.outTimeUs == 0) sample.outTimeUs = clock;
        sample.valid = true;
    } else if (key == "speed") {
        sample.speed = ParseSpeed(value);
        sample.valid = true;
    } else if (key == "total_size") {
        sample.totalBytes = ParseUInt64(value);
        sample.valid = true;
    } else if (key == "frame") {
        sample.frame = ParseInt64(value);
        sample.valid = true;
    } else if (key == "progress") {
        sample.finished = value == "end";
        sample.valid = true;
    }
    return sample;
}

} // namespace vsr::transcode
