#include "Probe.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <Infrastructure/Json.h>
#include <Infrastructure/Strings.h>

#include "ChildProcess.h"

namespace vsr::transcode {

namespace {

double LooseNumber(const json::Value& value) {
    if (value.GetType() == json::Type::Number) return value.AsNumber();
    if (value.GetType() == json::Type::String) {
        const std::string& text = value.AsString();
        if (text.empty()) return 0.0;
        char* end = nullptr;
        const double parsed = std::strtod(text.c_str(), &end);
        return end != text.c_str() ? parsed : 0.0;
    }
    return 0.0;
}

double ParseRational(const std::string& text) {
    const std::size_t slash = text.find('/');
    if (slash == std::string::npos) {
        char* end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        return end != text.c_str() ? value : 0.0;
    }
    const double numerator = std::strtod(text.substr(0, slash).c_str(), nullptr);
    const double denominator = std::strtod(text.substr(slash + 1).c_str(), nullptr);
    if (denominator <= 0.0) return 0.0;
    return numerator / denominator;
}

bool Truthy(const json::Value& value) {
    if (value.GetType() == json::Type::Number) return value.AsNumber() != 0.0;
    if (value.GetType() == json::Type::Boolean) return value.AsBool();
    return false;
}

std::wstring Quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

Result<std::wstring> LocateTool(const wchar_t* envName, const wchar_t* fileName) {
    wchar_t envPath[MAX_PATH] = {};
    if (GetEnvironmentVariableW(envName, envPath, MAX_PATH) > 0 &&
        GetFileAttributesW(envPath) != INVALID_FILE_ATTRIBUTES) {
        return std::wstring(envPath);
    }
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path candidate =
        std::filesystem::path(exePath).parent_path() / fileName;
    if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return candidate.wstring();
    }
    wchar_t found[MAX_PATH] = {};
    if (SearchPathW(nullptr, fileName, L".exe", MAX_PATH, found, nullptr) > 0) {
        return std::wstring(found);
    }
    candidate = std::filesystem::path(L"C:\\ffmpeg\\bin") / fileName;
    if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return candidate.wstring();
    }
    return Error(ErrorCode::EncoderUnavailable,
                 std::wstring(L"Missing ") + fileName +
                     L". Keep it next to MeetRec.exe or set the matching VSR_*_PATH "
                     L"environment variable.",
                 std::wstring(fileName) + L" not found (env, app dir, PATH, C:\\ffmpeg\\bin)");
}

} // namespace

std::uint64_t FileSizeBytes(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) return 0;
    return static_cast<std::uint64_t>(size);
}

Result<std::wstring> LocateFfprobe() {
    return LocateTool(L"VSR_FFPROBE_PATH", L"ffprobe.exe");
}

Result<std::wstring> LocateFfmpeg() {
    return LocateTool(L"VSR_FFMPEG_PATH", L"ffmpeg.exe");
}

std::wstring BuildProbeArguments(const std::wstring& input) {
    return L"-v error -print_format json -show_format -show_streams -i " + Quote(input);
}

Result<MediaInfo> ParseProbeJson(const std::string& jsonText) {
    bool parsed = false;
    const json::Value root = json::Parse(jsonText, &parsed);
    if (!parsed || root.GetType() != json::Type::Object) {
        std::string head = jsonText.substr(0, std::min<std::size_t>(120, jsonText.size()));
        return Error(ErrorCode::InternalState, L"This video could not be read.",
                     L"ffprobe returned malformed JSON (" +
                         std::to_wstring(jsonText.size()) + L" bytes): " +
                         Utf8ToWide(head));
    }

    MediaInfo info;
    const json::Value& format = root.Find("format");
    info.durationMs =
        static_cast<std::int64_t>(std::llround(LooseNumber(format.Find("duration")) * 1000.0));
    info.bitrateBps = static_cast<std::int64_t>(LooseNumber(format.Find("bit_rate")));
    info.fileBytes = static_cast<std::uint64_t>(LooseNumber(format.Find("size")));

    for (const json::Value& stream : root.Find("streams").GetArray()) {
        const std::string type = stream.Find("codec_type").AsString();
        if (type == "video" && !info.hasVideo) {
            // Cover art in audio files is a video stream too; skip it.
            const json::Value& disposition = stream.Find("disposition");
            if (Truthy(disposition.Find("attached_pic"))) continue;
            info.hasVideo = true;
            info.videoCodec = stream.Find("codec_name").AsString();
            info.width = static_cast<std::uint32_t>(
                std::max(0.0, LooseNumber(stream.Find("width"))));
            info.height = static_cast<std::uint32_t>(
                std::max(0.0, LooseNumber(stream.Find("height"))));
            info.frameRate = ParseRational(stream.Find("avg_frame_rate").AsString());
            if (info.frameRate <= 0.0) {
                info.frameRate = ParseRational(stream.Find("r_frame_rate").AsString());
            }
            if (info.durationMs <= 0) {
                info.durationMs = static_cast<std::int64_t>(
                    std::llround(LooseNumber(stream.Find("duration")) * 1000.0));
            }
        } else if (type == "audio" && !info.hasAudio) {
            info.hasAudio = true;
            info.audioCodec = stream.Find("codec_name").AsString();
            info.audioChannels = static_cast<std::uint32_t>(
                std::max(0.0, LooseNumber(stream.Find("channels"))));
        }
    }

    if (!info.hasVideo || info.width == 0 || info.height == 0) {
        return Error(ErrorCode::InternalState, L"This file has no readable video stream.",
                     L"ffprobe reported no usable video stream");
    }
    return info;
}

Result<MediaInfo> ProbeFile(const std::wstring& ffprobePath,
                            const std::filesystem::path& input, std::uint32_t timeoutMs) {
    auto child = RunChildCapture(ffprobePath, BuildProbeArguments(input.wstring()), timeoutMs);
    if (!child) return child.TakeError();
    if (child.Value().timedOut) {
        return Error(ErrorCode::InternalState, L"Reading this video timed out.",
                     L"ffprobe exceeded its deadline");
    }
    if (child.Value().exitCode != 0) {
        std::wstring detail = Utf8ToWide(child.Value().stderrText);
        if (detail.size() > 400) detail.resize(400);
        std::wstring stdoutHead = Utf8ToWide(child.Value().stdoutText.substr(
            0, std::min<std::size_t>(200, child.Value().stdoutText.size())));
        return Error(ErrorCode::InternalState, L"This video could not be read.",
                     L"ffprobe exited with " +
                         std::to_wstring(child.Value().exitCode) + L" (stdout " +
                         std::to_wstring(child.Value().stdoutText.size()) +
                         L" bytes): " + detail + L" | " + stdoutHead);
    }
    auto parsedInfo = ParseProbeJson(child.Value().stdoutText);
    if (!parsedInfo) return parsedInfo.TakeError();
    MediaInfo info = parsedInfo.Value();
    info.fileBytes = FileSizeBytes(input);
    return info;
}

} // namespace vsr::transcode
