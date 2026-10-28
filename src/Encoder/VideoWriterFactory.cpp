#include "VideoWriterFactory.h"

#include "FfmpegWriter.h"
#include "H264Mp4Writer.h"

namespace vsr::encoder {

WriterBackend WriterBackendFromString(const std::string& s) noexcept {
    if (s == "ffmpeg") return WriterBackend::Ffmpeg;
    if (s == "mf") return WriterBackend::MediaFoundation;
    return WriterBackend::Auto;
}

const char* ToString(WriterBackend backend) noexcept {
    switch (backend) {
        case WriterBackend::MediaFoundation: return "mf";
        case WriterBackend::Ffmpeg: return "ffmpeg";
        case WriterBackend::Auto: break;
    }
    return "auto";
}

WriterBackend ResolveBackend(WriterBackend backend) {
    if (backend != WriterBackend::Auto) return backend;
    // Prefer the ffmpeg backend: it bypasses the OS media stack whose
    // encoder-MFT failure family is documented on Insider builds, and it
    // degrades to Media Foundation when ffmpeg.exe is not available.
    auto ffmpeg = FfmpegWriter::LocateExecutable();
    return ffmpeg.IsOk() ? WriterBackend::Ffmpeg : WriterBackend::MediaFoundation;
}

Result<std::unique_ptr<IVideoWriter>> CreateWriter(
    WriterBackend backend, const capture::GraphicsDevice& device,
    const EncoderCapability& encoder, const PresetParams& preset,
    const std::filesystem::path& partialPath, const AudioFormat* audioFormat) {
    const WriterBackend resolved = ResolveBackend(backend);
    if (resolved == WriterBackend::Ffmpeg) {
        auto exe = FfmpegWriter::LocateExecutable();
        if (!exe) return exe.TakeError();
        auto writer = FfmpegWriter::Create(exe.Value(), preset, partialPath, audioFormat);
        if (!writer) return writer.TakeError();
        std::unique_ptr<IVideoWriter> upcast(writer.Value().release());
        return Result<std::unique_ptr<IVideoWriter>>(std::move(upcast));
    }
    auto writer = H264Mp4Writer::Create(device, encoder, preset, partialPath, audioFormat);
    if (!writer) return writer.TakeError();
    std::unique_ptr<IVideoWriter> upcast(writer.Value().release());
    return Result<std::unique_ptr<IVideoWriter>>(std::move(upcast));
}

} // namespace vsr::encoder
