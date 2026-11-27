#include "SenseVoiceRecognizer.h"

#include <Infrastructure/Strings.h>

#include <sherpa-onnx/c-api/c-api.h>

#include <algorithm>
#include <cstring>

namespace vsr::speech {

struct SenseVoiceRecognizer::Impl {
    const SherpaOnnxOfflineRecognizer* recognizer = nullptr;
    std::string modelPath;
    std::string tokensPath;
};

SenseVoiceRecognizer::SenseVoiceRecognizer() : impl_(std::make_unique<Impl>()) {}

SenseVoiceRecognizer::~SenseVoiceRecognizer() {
    Destroy();
}

bool SenseVoiceRecognizer::Create(const std::filesystem::path& model,
                                  const std::filesystem::path& tokens,
                                  int numThreads, std::wstring& error) {
    Destroy();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(model, ec) || ec) {
        error = L"SenseVoice model file is missing: " + model.wstring();
        return false;
    }
    if (!std::filesystem::is_regular_file(tokens, ec) || ec) {
        error = L"SenseVoice tokens file is missing: " + tokens.wstring();
        return false;
    }

    impl_->modelPath = WideToUtf8(model.wstring());
    impl_->tokensPath = WideToUtf8(tokens.wstring());

    SherpaOnnxOfflineRecognizerConfig config;
    std::memset(&config, 0, sizeof(config));
    config.feat_config.sample_rate = 16000;
    config.feat_config.feature_dim = 80;
    config.model_config.sense_voice.model = impl_->modelPath.c_str();
    config.model_config.sense_voice.language = "auto";
    config.model_config.sense_voice.use_itn = 1;
    config.model_config.tokens = impl_->tokensPath.c_str();
    config.model_config.num_threads =
        std::max(1, std::min(numThreads, 8));
    config.model_config.provider = "cpu";
    config.model_config.debug = 0;
    config.decoding_method = "greedy_search";
    config.max_active_paths = 4;

    impl_->recognizer = SherpaOnnxCreateOfflineRecognizer(&config);
    if (impl_->recognizer == nullptr) {
        error = L"SenseVoice recognizer could not be created (model/tokens mismatch?).";
        return false;
    }
    return true;
}

void SenseVoiceRecognizer::Destroy() {
    if (impl_ && impl_->recognizer != nullptr) {
        SherpaOnnxDestroyOfflineRecognizer(impl_->recognizer);
        impl_->recognizer = nullptr;
    }
}

bool SenseVoiceRecognizer::IsReady() const noexcept {
    return impl_ && impl_->recognizer != nullptr;
}

bool SenseVoiceRecognizer::Recognize(const float* samples, int frameCount,
                                     RecognizedSpeech& result,
                                     std::wstring& error) {
    result = RecognizedSpeech{};
    if (impl_->recognizer == nullptr) {
        error = L"SenseVoice recognizer is not initialized.";
        return false;
    }
    if (samples == nullptr || frameCount <= 0) {
        return true;
    }

    const SherpaOnnxOfflineStream* stream =
        SherpaOnnxCreateOfflineStream(impl_->recognizer);
    if (stream == nullptr) {
        error = L"SenseVoice stream creation failed.";
        return false;
    }

    SherpaOnnxAcceptWaveformOffline(stream, 16000, samples, frameCount);
    SherpaOnnxDecodeOfflineStream(impl_->recognizer, stream);

    const SherpaOnnxOfflineRecognizerResult* recognized =
        SherpaOnnxGetOfflineStreamResult(stream);
    if (recognized == nullptr) {
        SherpaOnnxDestroyOfflineStream(stream);
        error = L"SenseVoice decoding returned no result.";
        return false;
    }

    if (recognized->text != nullptr) {
        result.text = Utf8ToWide(recognized->text);
    }
    if (recognized->lang != nullptr) {
        result.language = recognized->lang;
    }

    SherpaOnnxDestroyOfflineRecognizerResult(recognized);
    SherpaOnnxDestroyOfflineStream(stream);
    return true;
}

} // namespace vsr::speech
