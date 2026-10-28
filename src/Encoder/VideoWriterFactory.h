// Virtual Screen Recorder - recording backend selection and writer factory.
#pragma once

#include <memory>
#include <string>

#include <Capture/GraphicsDevice.h>
#include "AudioCapture.h"
#include "EncoderProbe.h"
#include "IVideoWriter.h"
#include "VideoPreset.h"

namespace vsr::encoder {

enum class WriterBackend { Auto, MediaFoundation, Ffmpeg };

// "auto" / "mf" / "ffmpeg"; anything else falls back to Auto.
WriterBackend WriterBackendFromString(const std::string& s) noexcept;
const char* ToString(WriterBackend backend) noexcept;

// Resolves Auto against the machine (ffmpeg.exe available -> Ffmpeg, else
// Media Foundation); concrete backends pass through.
WriterBackend ResolveBackend(WriterBackend backend);

// Creates the recording sink. Auto resolves to the ffmpeg backend when an
// ffmpeg.exe can be located and to Media Foundation otherwise. The attempt
// matrices (audio on -> audio off, GPU -> CPU staging for MF) live inside
// the backend Create implementations; on total failure no usable recording
// has been started (FR-063).
Result<std::unique_ptr<IVideoWriter>> CreateWriter(
    WriterBackend backend, const capture::GraphicsDevice& device,
    const EncoderCapability& encoder, const PresetParams& preset,
    const std::filesystem::path& partialPath, const AudioFormat* audioFormat);

} // namespace vsr::encoder
