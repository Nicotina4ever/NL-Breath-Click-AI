// SPDX-License-Identifier: MPL-2.0
#include "respiro_onnx.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "core/dsp/resampler.h"
#include "kiss_fftr.h"
#include "onnxruntime_c_api.h"

namespace bc {
namespace studio {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kSampleRate = 16000;
constexpr int kFft = 400;
constexpr int kHop = 160;
constexpr int kMelBands = 128;
constexpr int kChannels = 3;

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), needed, nullptr, nullptr);
    return out;
}

std::filesystem::path executableDirectory() {
#ifdef _WIN32
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

std::string resolveModelPath(const std::string& configured) {
    if (!configured.empty()) {
        std::error_code ec;
        const auto path = std::filesystem::u8path(configured);
        return std::filesystem::is_regular_file(path, ec) ? path.u8string() : std::string{};
    }
    std::vector<std::filesystem::path> candidates;
    const auto exe = executableDirectory();
    candidates.push_back(exe / "models" / "generale" / "respiro-en-dinamico.onnx");
    candidates.push_back(exe / "models" / "generale" / "respiro-en.onnx");
    candidates.push_back(exe / "respiro-en-dinamico.onnx");
    candidates.push_back(exe / "respiro-en.onnx");
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec)) return candidate.u8string();
    }
    return {};
}

std::string resolveRuntimeDll() {
#ifdef _WIN32
    const auto exe = executableDirectory();
    std::vector<std::filesystem::path> candidates = {
        exe / "onnxruntime.dll",
        exe / "runtime" / "onnxruntime.dll",
    };
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec)) return candidate.u8string();
    }
#endif
    return {};
}

double hzToMel(double hz) {
    const double fSp = 200.0 / 3.0;
    const double minLogHz = 1000.0;
    const double minLogMel = minLogHz / fSp;
    const double logstep = std::log(6.4) / 27.0;
    return hz >= minLogHz ? minLogMel + std::log(hz / minLogHz) / logstep : hz / fSp;
}

double melToHz(double mel) {
    const double fSp = 200.0 / 3.0;
    const double minLogHz = 1000.0;
    const double minLogMel = minLogHz / fSp;
    const double logstep = std::log(6.4) / 27.0;
    return mel >= minLogMel ? minLogHz * std::exp(logstep * (mel - minLogMel)) : mel * fSp;
}

std::vector<float> makeMelFilterbank() {
    const int bins = kFft / 2 + 1;
    const double fmin = 0.0;
    const double fmax = kSampleRate / 2.0;
    const double melMin = hzToMel(fmin);
    const double melMax = hzToMel(fmax);
    std::vector<double> melPoints(static_cast<size_t>(kMelBands + 2));
    for (int i = 0; i < kMelBands + 2; ++i) {
        melPoints[static_cast<size_t>(i)] =
            melMin + (melMax - melMin) * static_cast<double>(i) / (kMelBands + 1);
    }
    std::vector<double> hzPoints(melPoints.size());
    for (size_t i = 0; i < melPoints.size(); ++i) hzPoints[i] = melToHz(melPoints[i]);

    std::vector<float> weights(static_cast<size_t>(kMelBands * bins), 0.0f);
    for (int m = 0; m < kMelBands; ++m) {
        const double left = hzPoints[static_cast<size_t>(m)];
        const double center = hzPoints[static_cast<size_t>(m + 1)];
        const double right = hzPoints[static_cast<size_t>(m + 2)];
        const double enorm = 2.0 / (right - left);
        for (int k = 0; k < bins; ++k) {
            const double freq = static_cast<double>(k) * kSampleRate / kFft;
            double weight = 0.0;
            if (freq > left && freq < right) {
                if (freq <= center) {
                    weight = (freq - left) / (center - left);
                } else {
                    weight = (right - freq) / (right - center);
                }
            }
            weights[static_cast<size_t>(m * bins + k)] =
                static_cast<float>(weight * enorm);
        }
    }
    return weights;
}

int frameCount(int64_t samples) {
    if (samples <= 0) return 0;
    return 1 + static_cast<int>(samples / kHop);
}

std::vector<float> logMelPower(const std::vector<float>& signal,
                               const std::vector<float>& filterbank,
                               int frames) {
    const int bins = kFft / 2 + 1;
    kiss_fftr_cfg config = kiss_fftr_alloc(kFft, 0, nullptr, nullptr);
    if (!config) return {};

    std::vector<kiss_fft_scalar> input(static_cast<size_t>(kFft));
    std::vector<kiss_fft_cpx> spectrum(static_cast<size_t>(bins));
    std::vector<float> power(static_cast<size_t>(frames * bins), 0.0f);
    std::vector<float> window(static_cast<size_t>(kFft));
    for (int i = 0; i < kFft; ++i) {
        window[static_cast<size_t>(i)] =
            0.5f - 0.5f * static_cast<float>(std::cos(2.0 * kPi * i / kFft));
    }

    const int pad = kFft / 2;
    for (int frame = 0; frame < frames; ++frame) {
        const int64_t start = static_cast<int64_t>(frame) * kHop - pad;
        for (int i = 0; i < kFft; ++i) {
            const int64_t index = start + i;
            const float value = (index >= 0 && index < static_cast<int64_t>(signal.size()))
                                    ? signal[static_cast<size_t>(index)]
                                    : 0.0f;
            input[static_cast<size_t>(i)] = value * window[static_cast<size_t>(i)];
        }
        kiss_fftr(config, input.data(), spectrum.data());
        for (int k = 0; k < bins; ++k) {
            const float re = spectrum[static_cast<size_t>(k)].r;
            const float im = spectrum[static_cast<size_t>(k)].i;
            power[static_cast<size_t>(frame * bins + k)] = re * re + im * im;
        }
    }
    kiss_fftr_free(config);

    std::vector<float> mel(static_cast<size_t>(frames * kMelBands), 0.0f);
    float maximum = 0.0f;
    for (int frame = 0; frame < frames; ++frame) {
        for (int m = 0; m < kMelBands; ++m) {
            float sum = 0.0f;
            for (int k = 0; k < bins; ++k) {
                sum += filterbank[static_cast<size_t>(m * bins + k)] *
                       power[static_cast<size_t>(frame * bins + k)];
            }
            mel[static_cast<size_t>(frame * kMelBands + m)] = sum;
            maximum = std::max(maximum, sum);
        }
    }

    const float reference = std::max(maximum, 1e-10f);
    const float refDb = 10.0f * std::log10(reference);
    for (float& value : mel) {
        const float safe = std::max(value, 1e-10f);
        value = std::max(-80.0f, 10.0f * std::log10(safe) - refDb);
    }
    return mel;
}

std::vector<float> zeroCrossingRate(const std::vector<float>& signal, int frames) {
    std::vector<float> zcr(static_cast<size_t>(frames), 0.0f);
    const int pad = kFft / 2;
    for (int frame = 0; frame < frames; ++frame) {
        const int64_t start = static_cast<int64_t>(frame) * kHop - pad;
        int crossings = 0;
        for (int i = 1; i < kFft; ++i) {
            const int64_t previousIndex = start + i - 1;
            const int64_t currentIndex = start + i;
            const float previous =
                (previousIndex >= 0 && previousIndex < static_cast<int64_t>(signal.size()))
                    ? signal[static_cast<size_t>(previousIndex)] : 0.0f;
            const float current =
                (currentIndex >= 0 && currentIndex < static_cast<int64_t>(signal.size()))
                    ? signal[static_cast<size_t>(currentIndex)] : 0.0f;
            const float signPrevious = previous > 0.0f ? 1.0f : (previous < 0.0f ? -1.0f : 0.0f);
            const float signCurrent = current > 0.0f ? 1.0f : (current < 0.0f ? -1.0f : 0.0f);
            crossings += static_cast<int>(std::fabs(signCurrent - signPrevious));
        }
        zcr[static_cast<size_t>(frame)] =
            static_cast<float>(crossings) * 0.5f / static_cast<float>(kFft);
    }
    return zcr;
}

bool buildFeatures(const std::vector<float>& mono, int sampleRate,
                   std::vector<float>* feature, int* frames,
                   std::string* error) {
    if (mono.empty()) {
        if (error) *error = "audio vuoto per Respiro-en";
        return false;
    }
    std::vector<float> resampled;
    if (sampleRate == kSampleRate) {
        resampled = mono;
    } else {
        LinearResampler resampler;
        if (!resampler.configure(static_cast<double>(sampleRate), kSampleRate)) {
            if (error) *error = "ricampionamento Respiro-en non riuscito";
            return false;
        }
        resampler.process(mono.data(), mono.size(), resampled);
    }
    const int count = frameCount(static_cast<int64_t>(resampled.size()));
    if (count < 2) {
        if (error) *error = "audio troppo breve per Respiro-en";
        return false;
    }

    static const std::vector<float> filters = makeMelFilterbank();
    std::vector<float> mel = logMelPower(resampled, filters, count);
    std::vector<float> zcr = zeroCrossingRate(resampled, count);
    if (mel.empty()) {
        if (error) *error = "mel-spettrogramma non calcolabile";
        return false;
    }

    feature->assign(static_cast<size_t>(kChannels * kMelBands * count), 0.0f);
    for (int frame = 0; frame < count; ++frame) {
        double mean = 0.0;
        for (int m = 0; m < kMelBands; ++m) {
            mean += mel[static_cast<size_t>(frame * kMelBands + m)];
        }
        mean /= kMelBands;
        double variance = 0.0;
        for (int m = 0; m < kMelBands; ++m) {
            const double delta = mel[static_cast<size_t>(frame * kMelBands + m)] - mean;
            variance += delta * delta;
        }
        variance /= (kMelBands - 1); // torch.var default unbiased
        for (int m = 0; m < kMelBands; ++m) {
            (*feature)[static_cast<size_t>((0 * kMelBands + m) * count + frame)] =
                mel[static_cast<size_t>(frame * kMelBands + m)];
            (*feature)[static_cast<size_t>((1 * kMelBands + m) * count + frame)] =
                static_cast<float>(variance);
            (*feature)[static_cast<size_t>((2 * kMelBands + m) * count + frame)] =
                zcr[static_cast<size_t>(frame)];
        }
    }
    *frames = count;
    return true;
}

struct OrtHolder {
    HMODULE module = nullptr;
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    OrtSessionOptions* options = nullptr;
    OrtSession* session = nullptr;
    OrtAllocator* allocator = nullptr;

    ~OrtHolder() {
        if (session) api->ReleaseSession(session);
        if (options) api->ReleaseSessionOptions(options);
        if (env) api->ReleaseEnv(env);
        if (module) FreeLibrary(module);
    }
};

bool checkStatus(const OrtApi* api, OrtStatus* status, const std::string& operation,
                 std::string* error) {
    if (!status) return true;
    if (error) *error = operation + ": " + api->GetErrorMessage(status);
    api->ReleaseStatus(status);
    return false;
}

bool loadRuntime(OrtHolder& holder, std::string* error) {
#ifdef _WIN32
    const std::string dll = resolveRuntimeDll();
    if (dll.empty()) {
        if (error) *error = "onnxruntime.dll non trovata accanto al programma";
        return false;
    }
    holder.module = LoadLibraryW(std::filesystem::u8path(dll).wstring().c_str());
    if (!holder.module) {
        if (error) *error = "onnxruntime.dll non caricabile";
        return false;
    }
    using GetApiBase = const OrtApiBase* (ORT_API_CALL*)();
    auto getter = reinterpret_cast<GetApiBase>(GetProcAddress(holder.module, "OrtGetApiBase"));
    if (!getter) {
        if (error) *error = "OrtGetApiBase non trovata";
        return false;
    }
    holder.api = getter()->GetApi(ORT_API_VERSION);
    if (!holder.api) {
        if (error) *error = "ONNX Runtime troppo vecchio per l'API richiesta";
        return false;
    }
    return true;
#else
    (void)holder;
    if (error) *error = "ONNX Runtime disponibile solo su Windows";
    return false;
#endif
}

bool createSession(OrtHolder& holder, const std::string& modelPath, std::string* error) {
    const OrtApi* api = holder.api;
    if (!checkStatus(api, api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "BreathClickStudio", &holder.env),
                     "creazione ambiente ONNX", error)) return false;
    if (!checkStatus(api, api->CreateSessionOptions(&holder.options),
                     "creazione opzioni ONNX", error)) return false;
    api->SetIntraOpNumThreads(holder.options, 1);
    api->SetSessionGraphOptimizationLevel(holder.options, ORT_ENABLE_ALL);
    const std::wstring wideModel = std::filesystem::u8path(modelPath).wstring();
    if (!checkStatus(api, api->CreateSession(holder.env, wideModel.c_str(), holder.options,
                                             &holder.session),
                     "apertura modello Respiro-en", error)) return false;
    if (!checkStatus(api, api->GetAllocatorWithDefaultOptions(&holder.allocator),
                     "allocator ONNX", error)) return false;
    return true;
}

} // namespace

bool respiroEnAvailable(const std::string& modelPath, std::string* resolvedPath,
                        std::string* error) {
    const std::string resolved = resolveModelPath(modelPath);
    if (resolved.empty()) {
        if (error) *error = "modello Respiro-en non trovato";
        return false;
    }
    if (resolvedPath) *resolvedPath = resolved;
    const std::string dll = resolveRuntimeDll();
    if (dll.empty()) {
        if (error) *error = "onnxruntime.dll non trovata";
        return false;
    }
    return true;
}

bool respiroEnDetect(const std::vector<float>& mono, int sampleRate,
                     const std::string& modelPath, float threshold,
                     std::vector<AiEvent>* events, std::vector<float>* probabilities,
                     std::string* error) {
    if (events) events->clear();
    if (probabilities) probabilities->clear();

    std::string resolved;
    if (!respiroEnAvailable(modelPath, &resolved, error)) return false;

    std::vector<float> feature;
    int frames = 0;
    if (!buildFeatures(mono, sampleRate, &feature, &frames, error)) return false;

    OrtHolder holder;
    if (!loadRuntime(holder, error)) return false;
    if (!createSession(holder, resolved, error)) return false;
    const OrtApi* api = holder.api;

    const int64_t featureDims[4] = {1, kChannels, kMelBands, frames};
    const int64_t lengthDims[1] = {1};
    int64_t lengthValue = frames;
    OrtValue* featureValue = nullptr;
    OrtValue* lengthValueOrt = nullptr;
    OrtMemoryInfo* memory = nullptr;
    if (!checkStatus(api, api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory),
                     "memoria ONNX", error)) return false;
    if (!checkStatus(api, api->CreateTensorWithDataAsOrtValue(
                              memory, feature.data(), feature.size() * sizeof(float),
                              featureDims, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                              &featureValue),
                     "tensore feature ONNX", error)) {
        api->ReleaseMemoryInfo(memory);
        return false;
    }
    if (!checkStatus(api, api->CreateTensorWithDataAsOrtValue(
                              memory, &lengthValue, sizeof(lengthValue),
                              lengthDims, 1, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,
                              &lengthValueOrt),
                     "tensore length ONNX", error)) {
        api->ReleaseMemoryInfo(memory);
        api->ReleaseValue(featureValue);
        return false;
    }
    api->ReleaseMemoryInfo(memory);

    const char* inputNames[2] = {"feature", "length"};
    const char* outputNames[1] = {"breath_probability"};
    OrtValue* inputValues[2] = {featureValue, lengthValueOrt};
    OrtValue* outputValue = nullptr;
    const bool runOk = checkStatus(api, api->Run(holder.session, nullptr, inputNames,
                                                inputValues, 2, outputNames, 1,
                                                &outputValue),
                                  "inferenza Respiro-en", error);
    api->ReleaseValue(featureValue);
    api->ReleaseValue(lengthValueOrt);
    if (!runOk) return false;

    OrtTensorTypeAndShapeInfo* shapeInfo = nullptr;
    if (!checkStatus(api, api->GetTensorTypeAndShape(outputValue, &shapeInfo),
                     "forma output ONNX", error)) {
        api->ReleaseValue(outputValue);
        return false;
    }
    size_t dimensions = 0;
    api->GetDimensionsCount(shapeInfo, &dimensions);
    std::vector<int64_t> shape(dimensions, 0);
    api->GetDimensions(shapeInfo, shape.data(), dimensions);
    api->ReleaseTensorTypeAndShapeInfo(shapeInfo);
    if (shape.size() != 2 || shape[1] <= 0) {
        if (error) *error = "output Respiro-en con forma inattesa";
        api->ReleaseValue(outputValue);
        return false;
    }
    const int outputFrames = static_cast<int>(shape[1]);
    void* rawOutput = nullptr;
    if (!checkStatus(api, api->GetTensorMutableData(outputValue, &rawOutput),
                     "lettura output ONNX", error)) {
        api->ReleaseValue(outputValue);
        return false;
    }
    const float* data = static_cast<const float*>(rawOutput);

    std::vector<float> output(static_cast<size_t>(outputFrames));
    for (int i = 0; i < outputFrames; ++i) output[static_cast<size_t>(i)] = data[i];
    api->ReleaseValue(outputValue);
    if (probabilities) *probabilities = output;

    // Il modello Respiro-en predice un frame ogni 10 ms. Il codice ufficiale
    // scarta i gruppi di <= 20 frame (200 ms).
    int start = -1;
    float peak = 0.0f;
    for (int i = 0; i <= outputFrames; ++i) {
        const bool active = i < outputFrames && output[static_cast<size_t>(i)] > threshold;
        if (active) {
            if (start < 0) {
                start = i;
                peak = output[static_cast<size_t>(i)];
            } else {
                peak = std::max(peak, output[static_cast<size_t>(i)]);
            }
        } else if (start >= 0) {
            const int end = i;
            if (end - start > 20) {
                AiEvent event;
                event.startFrame = start;
                event.endFrame = end;
                event.score = peak;
                if (events) events->push_back(event);
            }
            start = -1;
            peak = 0.0f;
        }
    }
    return true;
}

} // namespace studio
} // namespace bc
