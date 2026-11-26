#include "SpeechVad.h"

#include <Infrastructure/Strings.h>

#include <sherpa-onnx/c-api/c-api.h>

#include <algorithm>
#include <cstring>

namespace vsr::speech {

struct SpeechVad::Impl {
    const SherpaOnnxVoiceActivityDetector* vad = nullptr;
    std::string modelPath;
};

SpeechVad::SpeechVad() : impl_(std::make_unique<Impl>()) {}

SpeechVad::~SpeechVad() {
    Destroy();
}

bool SpeechVad::Create(const std::filesystem::path& model, std::wstring& error) {
    Destroy();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(model, ec) || ec) {
        error = L"Silero VAD model is missing: " + model.wstring();
        return false;
    }
    impl_->modelPath = WideToUtf8(model.wstring());

    SherpaOnnxVadModelConfig config;
    std::memset(&config, 0, sizeof(config));
    config.silero_vad.model = impl_->modelPath.c_str();
    config.silero_vad.threshold = 0.40f;
    config.silero_vad.min_silence_duration = 0.50f;
    config.silero_vad.min_speech_duration = 0.25f;
    config.silero_vad.max_speech_duration = 15.0f;
    config.silero_vad.window_size = 512;
    config.sample_rate = 16000;
    config.num_threads = 1;
    config.provider = "cpu";
    config.debug = 0;

    impl_->vad = SherpaOnnxCreateVoiceActivityDetector(&config, 30.0f);
    if (impl_->vad == nullptr) {
        error = L"Silero VAD could not be created (invalid model?).";
        return false;
    }
    return true;
}

void SpeechVad::Destroy() {
    if (impl_ && impl_->vad != nullptr) {
        SherpaOnnxDestroyVoiceActivityDetector(impl_->vad);
        impl_->vad = nullptr;
    }
}

void SpeechVad::Accept(const float* samples, int sampleCount) {
    if (impl_->vad == nullptr || samples == nullptr || sampleCount <= 0) return;
    SherpaOnnxVoiceActivityDetectorAcceptWaveform(impl_->vad, samples, sampleCount);
}

bool SpeechVad::Pop(VadSpeechSegment& segment) {
    segment = VadSpeechSegment{};
    if (impl_->vad == nullptr) return false;
    if (SherpaOnnxVoiceActivityDetectorEmpty(impl_->vad) != 0) return false;

    const SherpaOnnxSpeechSegment* front =
        SherpaOnnxVoiceActivityDetectorFront(impl_->vad);
    if (front == nullptr) {
        SherpaOnnxVoiceActivityDetectorPop(impl_->vad);
        return false;
    }
    segment.startSample = front->start;
    if (front->samples != nullptr && front->n > 0) {
        segment.samples.assign(front->samples, front->samples + front->n);
    }
    SherpaOnnxDestroySpeechSegment(front);
    SherpaOnnxVoiceActivityDetectorPop(impl_->vad);
    return !segment.samples.empty();
}

void SpeechVad::Flush() {
    if (impl_->vad != nullptr) {
        SherpaOnnxVoiceActivityDetectorFlush(impl_->vad);
    }
}

void SpeechVad::Reset() {
    if (impl_->vad != nullptr) {
        SherpaOnnxVoiceActivityDetectorReset(impl_->vad);
    }
}

} // namespace vsr::speech
