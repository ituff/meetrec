#include "SpeechSelfTest.h"

#include <Infrastructure/Log.h>
#include <Infrastructure/Paths.h>
#include <Infrastructure/Strings.h>
#include <Speech/SpeechModelPack.h>
#include <Speech/SpeechSession.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace vsr::app {

namespace {

bool ReadPcm16Wav(const std::filesystem::path& path, std::uint32_t& sampleRate,
                  std::vector<std::int16_t>& mono, std::wstring& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        error = L"Could not open " + path.wstring();
        return false;
    }

    char riff[12] = {};
    in.read(riff, sizeof(riff));
    if (in.gcount() != static_cast<std::streamsize>(sizeof(riff)) ||
        std::string(riff, 4) != "RIFF" || std::string(riff + 8, 4) != "WAVE") {
        error = L"Not a RIFF/WAVE file: " + path.wstring();
        return false;
    }

    std::uint16_t format = 0;
    std::uint16_t channels = 0;
    std::uint16_t bitsPerSample = 0;
    bool haveFormat = false;

    while (in.good()) {
        char id[4] = {};
        std::uint32_t size = 0;
        in.read(id, sizeof(id));
        in.read(reinterpret_cast<char*>(&size), sizeof(size));
        if (in.gcount() != static_cast<std::streamsize>(sizeof(size))) break;

        if (std::string(id, 4) == "fmt ") {
            if (size < 16) {
                error = L"Malformed WAV fmt chunk.";
                return false;
            }
            std::vector<char> fmt(size);
            in.read(fmt.data(), static_cast<std::streamsize>(size));
            std::memcpy(&format, fmt.data(), sizeof(format));
            std::memcpy(&channels, fmt.data() + 2, sizeof(channels));
            std::memcpy(&sampleRate, fmt.data() + 4, sizeof(sampleRate));
            std::memcpy(&bitsPerSample, fmt.data() + 14, sizeof(bitsPerSample));
            haveFormat = true;
        } else if (std::string(id, 4) == "data") {
            if (!haveFormat || format != 1 || bitsPerSample != 16 || channels == 0 ||
                sampleRate == 0) {
                error = L"Only 16-bit PCM WAV is supported (format=" +
                        std::to_wstring(format) + L", bits=" +
                        std::to_wstring(bitsPerSample) + L").";
                return false;
            }
            std::vector<std::int16_t> interleaved(size / sizeof(std::int16_t));
            in.read(reinterpret_cast<char*>(interleaved.data()),
                    static_cast<std::streamsize>(interleaved.size() *
                                                 sizeof(std::int16_t)));
            const std::size_t frames = interleaved.size() / channels;
            mono.resize(frames);
            for (std::size_t frame = 0; frame < frames; ++frame) {
                std::int32_t sum = 0;
                for (std::uint16_t channel = 0; channel < channels; ++channel) {
                    sum += interleaved[frame * channels + channel];
                }
                mono[frame] = static_cast<std::int16_t>(
                    sum / static_cast<std::int32_t>(channels));
            }
            return true;
        } else {
            in.seekg(size, std::ios::cur);
        }
        if (size % 2 != 0) in.seekg(1, std::ios::cur); // RIFF word alignment
    }
    error = L"No WAV data chunk found.";
    return false;
}

} // namespace

int RunSpeechSelfTest(int argc, wchar_t* argv[]) {
    LogConfig logConfig;
    logConfig.directory = LogDir();
    Logger::Instance().Configure(logConfig);

    if (argc < 3) {
        wprintf(L"usage: MeetRec --speech-test <wav> [--model <dir>] [--out <base>]\n");
        return 2;
    }

    const std::filesystem::path wav = argv[2];
    std::filesystem::path modelOverride;
    std::filesystem::path outputBase;
    for (int i = 3; i + 1 < argc; i += 2) {
        if (wcscmp(argv[i], L"--model") == 0) modelOverride = argv[i + 1];
        else if (wcscmp(argv[i], L"--out") == 0) outputBase = argv[i + 1];
    }
    if (outputBase.empty()) {
        outputBase = wav.parent_path() / (wav.stem().wstring() + L".meetrec-speech");
    }

    const speech::SpeechModelStatus model = speech::LocateSpeechModel(modelOverride);
    if (!model.ready) {
        wprintf(L"[speech-test] model pack not ready: %s\n  dir: %s\n",
                model.detail.c_str(), model.paths.directory.c_str());
        return 1;
    }

    std::uint32_t sampleRate = 0;
    std::vector<std::int16_t> mono;
    std::wstring error;
    if (!ReadPcm16Wav(wav, sampleRate, mono, error)) {
        wprintf(L"[speech-test] %s\n", error.c_str());
        return 1;
    }
    const double seconds = static_cast<double>(mono.size()) / sampleRate;
    wprintf(L"[speech-test] %zu frames, %u Hz, %.1f s\n", mono.size(), sampleRate,
            seconds);

    speech::SpeechSessionOptions options;
    options.model = model.paths;
    options.basePath = outputBase;
    options.sourceSampleRate = sampleRate;
    options.microphone = false;
    options.chinese = true;
    options.numThreads = 4;

    auto session = speech::SpeechSession::Start(options);
    if (!session) {
        const Error e = session.TakeError();
        wprintf(L"[speech-test] session start failed: %s\n", e.technicalMessage.c_str());
        return 1;
    }
    if (!session.Value()->WaitForReady(30'000)) {
        const auto status = session.Value()->GetStatus();
        wprintf(L"[speech-test] model load failed: %s\n", status.detail.c_str());
        return 1;
    }

    const std::size_t chunkFrames = std::max<std::size_t>(1, sampleRate / 5);
    std::size_t offset = 0;
    std::int64_t timestampUs = 0;
    while (offset < mono.size()) {
        const std::size_t frames = std::min(chunkFrames, mono.size() - offset);
        session.Value()->PushAudio(false, mono.data() + offset, frames, timestampUs);
        offset += frames;
        timestampUs += static_cast<std::int64_t>(frames) * 1'000'000 /
                       static_cast<std::int64_t>(sampleRate);
    }

    session.Value()->Stop();
    const speech::TranscriptStore::Written written = session.Value()->Finalize();
    const auto status = session.Value()->GetStatus();
    wprintf(L"[speech-test] state=%d segments=%zu\n", static_cast<int>(status.state),
            status.segments);
    wprintf(L"[speech-test] srt: %s\n", written.srt.c_str());
    wprintf(L"[speech-test] txt: %s\n", written.txt.c_str());
    return written.srt.empty() ? 1 : 0;
}

} // namespace vsr::app
