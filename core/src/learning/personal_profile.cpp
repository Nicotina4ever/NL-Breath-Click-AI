// SPDX-License-Identifier: MPL-2.0
#include "core/learning/personal_profile.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <set>
#include <cctype>
#include "core/util/atomic_file.h"
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif

namespace bc {
namespace {

float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

float sigmoid(float value) {
    if (value > 40.0f) return 1.0f;
    if (value < -40.0f) return 0.0f;
    return 1.0f / (1.0f + std::exp(-value));
}

std::vector<float> featureVector(const LearningFeatures& features) {
    // Feature normalizzate: il profilo non dipende dalla scala del file.
    const float duration = std::max(0.0f, std::min(2.0f, features.durationMs / 500.0f));
    const float rms = clamp01((features.rmsDb + 80.0f) / 80.0f);
    const float zcr = std::max(0.0f, std::min(1.0f, features.zeroCrossRate));
    const float mean = std::max(0.0f, std::min(1.0f, features.meanAbs * 4.0f));
    return {clamp01(features.modelScore), duration, rms, zcr, mean};
}

std::string timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local = {};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &local);
    return buffer;
}

// The identity is based on decoded PCM, so renaming/copying a recording or
// changing its container metadata cannot move it across the holdout boundary.
std::string recordingIdentity(const AudioBuffer& audio) {
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    bool ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    int format[] = {audio.sampleRate, audio.channels};
    if (ok) ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(format), sizeof(format), 0) >= 0;
    const auto* bytes = reinterpret_cast<const unsigned char*>(audio.samples.data());
    size_t remaining = audio.samples.size() * sizeof(float);
    while (ok && remaining) {
        const ULONG chunk = static_cast<ULONG>(std::min<size_t>(remaining, 1 << 20));
        ok = BCryptHashData(hash, const_cast<PUCHAR>(bytes), chunk, 0) >= 0;
        bytes += chunk; remaining -= chunk;
    }
    unsigned char digest[32] = {};
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) return {};
#else
    auto* context = EVP_MD_CTX_new();
    if (!context) return {};
    const int format[] = {audio.sampleRate, audio.channels};
    unsigned char digest[32] = {};
    unsigned int length = 0;
    const bool ok = EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1 &&
        EVP_DigestUpdate(context, format, sizeof(format)) == 1 &&
        EVP_DigestUpdate(context, audio.samples.data(), audio.samples.size() * sizeof(float)) == 1 &&
        EVP_DigestFinal_ex(context, digest, &length) == 1 && length == sizeof(digest);
    EVP_MD_CTX_free(context);
    if (!ok) return {};
#endif
    std::ostringstream out;
    for (auto byte : digest) out << std::hex << std::setw(2) << std::setfill('0') << int(byte);
    return out.str();
}

std::string sourceKey(const LearningExample& example) {
    if (!example.recordingId.empty()) return example.recordingId;
    std::error_code ec;
    auto path = std::filesystem::weakly_canonical(std::filesystem::u8path(example.sourcePath), ec);
    std::string key = (ec ? std::filesystem::u8path(example.sourcePath).lexically_normal() : path).generic_u8string();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return char(std::tolower(c)); });
#endif
    return key;
}

bool verified(const LearningExample& example) {
    const auto& f = example.features;
    return !example.sourcePath.empty() &&
        (example.label == LearningLabel::BreathConfirmed || example.label == LearningLabel::MissedBreath ||
         example.label == LearningLabel::FalsePositive) && std::isfinite(f.modelScore) &&
        std::isfinite(f.durationMs) && std::isfinite(f.rmsDb) &&
        std::isfinite(f.zeroCrossRate) && std::isfinite(f.meanAbs);
}

struct Split {
    std::vector<LearningExample> train;
    std::vector<LearningExample> validation;
    std::string validationSource;
};

bool splitExamples(const std::vector<LearningExample>& examples, Split& split) {
    std::vector<std::string> sources;
    for (const LearningExample& example : examples) {
        if (example.label != LearningLabel::BreathConfirmed &&
            example.label != LearningLabel::MissedBreath &&
            example.label != LearningLabel::FalsePositive) {
            continue;
        }
        sources.push_back(sourceKey(example));
    }
    std::sort(sources.begin(), sources.end());
    sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
    if (sources.size() < 2) return false;

    for (const std::string& candidate : sources) {
        Split attempt;
        attempt.validationSource = candidate;
        for (const LearningExample& example : examples) {
            if (example.label != LearningLabel::BreathConfirmed &&
                example.label != LearningLabel::MissedBreath &&
                example.label != LearningLabel::FalsePositive) {
                continue;
            }
            if (sourceKey(example) == candidate) attempt.validation.push_back(example);
            else attempt.train.push_back(example);
        }
        const int trainPos = static_cast<int>(std::count_if(
            attempt.train.begin(), attempt.train.end(), [](const LearningExample& e) {
                return e.label == LearningLabel::BreathConfirmed ||
                       e.label == LearningLabel::MissedBreath;
            }));
        const int trainNeg = static_cast<int>(attempt.train.size()) - trainPos;
        const int valPos = static_cast<int>(std::count_if(
            attempt.validation.begin(), attempt.validation.end(),
            [](const LearningExample& e) {
                return e.label == LearningLabel::BreathConfirmed ||
                       e.label == LearningLabel::MissedBreath;
            }));
        const int valNeg = static_cast<int>(attempt.validation.size()) - valPos;
        if (trainPos >= 3 && trainNeg >= 3 && valPos >= 2 && valNeg >= 2) {
            split = std::move(attempt);
            return true;
        }
    }
    return false;
}

float predictProba(const PersonalProfile& profile, const LearningFeatures& features) {
    const std::vector<float> x = featureVector(features);
    const float z = profile.bias +
                    profile.weightScore * x[0] +
                    profile.weightDuration * x[1] +
                    profile.weightRms * x[2] +
                    profile.weightZcr * x[3] +
                    profile.weightMean * x[4];
    return sigmoid(z);
}

} // namespace

const char* learningLabelName(LearningLabel label) {
    switch (label) {
        case LearningLabel::BreathConfirmed: return "breath_confirmed";
        case LearningLabel::FalsePositive: return "false_positive";
        case LearningLabel::MissedBreath: return "missed_breath";
        case LearningLabel::EditorialExclusion: return "editorial_exclusion";
        case LearningLabel::ClickConfirmed: return "click_confirmed";
        case LearningLabel::ClickFalsePositive: return "click_false_positive";
        case LearningLabel::MissedClick: return "missed_click";
        case LearningLabel::MouthClickConfirmed: return "mouth_click_confirmed";
        case LearningLabel::VocalTransient: return "vocal_transient";
        default: return "unverified";
    }
}

LearningLabel learningLabelFromName(const std::string& name) {
    if (name == "breath_confirmed") return LearningLabel::BreathConfirmed;
    if (name == "false_positive") return LearningLabel::FalsePositive;
    if (name == "missed_breath") return LearningLabel::MissedBreath;
    if (name == "editorial_exclusion") return LearningLabel::EditorialExclusion;
    if (name == "click_confirmed") return LearningLabel::ClickConfirmed;
    if (name == "click_false_positive") return LearningLabel::ClickFalsePositive;
    if (name == "missed_click") return LearningLabel::MissedClick;
    if (name == "mouth_click_confirmed") return LearningLabel::MouthClickConfirmed;
    if (name == "vocal_transient") return LearningLabel::VocalTransient;
    return LearningLabel::Unverified;
}

float PersonalProfile::probability(const LearningFeatures& features) const {
    return predictProba(*this, features);
}

LearningFeatures extractLearningFeatures(const AudioBuffer& audio,
                                         int64_t startFrame, int64_t endFrame,
                                         float modelScore) {
    LearningFeatures features;
    features.modelScore = clamp01(modelScore);
    const int channels = std::max(1, audio.channels);
    const int64_t total = audio.frameCount();
    const int64_t start = std::max<int64_t>(0, startFrame);
    const int64_t end = std::min<int64_t>(total, endFrame);
    if (end <= start) return features;
    double sumSquares = 0.0;
    double sumAbs = 0.0;
    int64_t crossings = 0;
    int64_t previousSign = 0;
    for (int64_t frame = start; frame < end; ++frame) {
        double mono = 0.0;
        for (int c = 0; c < channels; ++c) {
            const size_t index = static_cast<size_t>(frame * channels + c);
            if (index < audio.samples.size()) mono += audio.samples[index];
        }
        mono /= channels;
        sumSquares += mono * mono;
        sumAbs += std::abs(mono);
        const int64_t sign = mono > 0.0 ? 1 : (mono < 0.0 ? -1 : 0);
        if (sign != 0 && previousSign != 0 && sign != previousSign) ++crossings;
        if (sign != 0) previousSign = sign;
    }
    const int64_t frames = end - start;
    features.durationMs = 1000.0f * static_cast<float>(frames) / std::max(1, audio.sampleRate);
    const double rms = std::sqrt(sumSquares / std::max<int64_t>(1, frames));
    features.rmsDb = rms > 1e-12 ? static_cast<float>(20.0 * std::log10(rms)) : -160.0f;
    features.meanAbs = static_cast<float>(sumAbs / std::max<int64_t>(1, frames));
    features.zeroCrossRate = static_cast<float>(crossings) / std::max<int64_t>(1, frames);
    return features;
}

std::vector<LearningExample> collectLearningExamples(const AudioDocument& document) {
    std::vector<LearningExample> examples;
    const AudioBuffer& audio = document.audio();
    const std::string identity = recordingIdentity(audio);
    if (identity.empty()) return examples; // Cannot guarantee independent recordings.
    for (const EditRegion& region : document.regions()) {
        if (!region.trainingEligible || region.decisionSource != "user" ||
            region.kind != DetectionKind::Breath || region.manualOperation) continue;
        const LearningLabel label = learningLabelFromName(region.learningLabel);
        if (label == LearningLabel::Unverified || label == LearningLabel::EditorialExclusion) {
            continue;
        }
        LearningExample example;
        example.label = label;
        example.sourcePath = document.sourcePath();
        example.recordingId = identity;
        example.startFrame = region.startSample;
        example.endFrame = region.endSample;
        example.baselineAccepted = region.fromDetection;
        example.features = extractLearningFeatures(audio, region.startSample,
                                                   region.endSample, region.score);
        examples.push_back(example);
    }
    return examples;
}

bool trainPersonalProfile(const std::vector<LearningExample>& input,
                          PersonalProfile& profile, std::string* error) {
    profile = PersonalProfile{};
    std::vector<LearningExample> examples;
    std::map<std::string, LearningLabel> seen;
    for (const auto& example : input) {
        if (!verified(example)) continue;
        for (const auto& previous : examples) {
            const bool positive = example.label != LearningLabel::FalsePositive;
            const bool otherPositive = previous.label != LearningLabel::FalsePositive;
            if (sourceKey(example) == sourceKey(previous) && positive != otherPositive &&
                example.startFrame < previous.endFrame && previous.startFrame < example.endFrame) {
                if (error) *error = "etichette positive e negative sovrapposte: verificare i confini";
                return false;
            }
        }
        if (example.endFrame > example.startFrame) {
            const std::string key = sourceKey(example) + ":" + std::to_string(example.startFrame) +
                                    ":" + std::to_string(example.endFrame);
            auto found = seen.find(key);
            if (found != seen.end()) {
                if (found->second != example.label) {
                    if (error) *error = "etichette discordanti per lo stesso intervallo della registrazione";
                    return false;
                }
                continue;
            }
            seen.emplace(key, example.label);
        }
        examples.push_back(example);
    }
    const int positives = static_cast<int>(std::count_if(
        examples.begin(), examples.end(), [](const LearningExample& e) {
            return e.label == LearningLabel::BreathConfirmed ||
                   e.label == LearningLabel::MissedBreath;
        }));
    const int negatives = static_cast<int>(std::count_if(
        examples.begin(), examples.end(), [](const LearningExample& e) {
            return e.label == LearningLabel::FalsePositive;
        }));
    if (positives < 6 || negatives < 6) {
        if (error) {
            *error = "servono almeno 6 respiri confermati e 6 falsi positivi "
                     "verificati; trovati " + std::to_string(positives) + " e " +
                     std::to_string(negatives);
        }
        return false;
    }
    Split split;
    if (!splitExamples(examples, split)) {
        if (error) {
            *error = "servono esempi validati da almeno due registrazioni "
                     "distinte, con respiri e falsi positivi in entrambe";
        }
        return false;
    }

    float bias = 0.0f;
    float w0 = 0.0f;
    float w1 = 0.0f;
    float w2 = 0.0f;
    float w3 = 0.0f;
    float w4 = 0.0f;
    const float learningRate = 0.12f;
    for (int iteration = 0; iteration < 500; ++iteration) {
        float gradBias = 0.0f;
        float grad0 = 0.0f;
        float grad1 = 0.0f;
        float grad2 = 0.0f;
        float grad3 = 0.0f;
        float grad4 = 0.0f;
        for (const LearningExample& example : split.train) {
            const std::vector<float> x = featureVector(example.features);
            const float target = (example.label == LearningLabel::BreathConfirmed ||
                                  example.label == LearningLabel::MissedBreath) ? 1.0f : 0.0f;
            const float z = bias + w0 * x[0] + w1 * x[1] + w2 * x[2] + w3 * x[3] + w4 * x[4];
            const float p = sigmoid(z);
            const float d = p - target;
            gradBias += d;
            grad0 += d * x[0];
            grad1 += d * x[1];
            grad2 += d * x[2];
            grad3 += d * x[3];
            grad4 += d * x[4];
        }
        const float n = static_cast<float>(std::max<size_t>(1, split.train.size()));
        const float regularization = 0.005f;
        bias -= learningRate * gradBias / n;
        w0 -= learningRate * (grad0 / n + regularization * w0);
        w1 -= learningRate * (grad1 / n + regularization * w1);
        w2 -= learningRate * (grad2 / n + regularization * w2);
        w3 -= learningRate * (grad3 / n + regularization * w3);
        w4 -= learningRate * (grad4 / n + regularization * w4);
    }

    PersonalProfile candidate;
    candidate.valid = true;
    candidate.bias = bias;
    candidate.weightScore = w0;
    candidate.weightDuration = w1;
    candidate.weightRms = w2;
    candidate.weightZcr = w3;
    candidate.weightMean = w4;
    candidate.valid = true;

    struct Metrics {
        int tp = 0, tn = 0, fp = 0, fn = 0;
        double accuracy() const {
            return .5 * (double(tp) / std::max(1, tp + fn) + double(tn) / std::max(1, tn + fp));
        }
    };
    auto eval = [&](const std::vector<LearningExample>& data, float threshold, bool general = false) {
        Metrics metric;
        for (const auto& example : data) {
            const bool positive = example.label != LearningLabel::FalsePositive;
            // Runtime filters existing proposals; it cannot generate a missed
            // breath. Validation must measure that same operating behavior.
            const bool predicted = example.baselineAccepted && (general ||
                predictProba(candidate, example.features) >= threshold);
            if (positive && predicted) ++metric.tp;
            else if (positive) ++metric.fn;
            else if (!predicted) ++metric.tn;
            else ++metric.fp;
        }
        return metric;
    };

    // Select the operating threshold using training recordings ONLY. Holdout
    // labels are read once below to accept/reject the fixed candidate.
    float bestThreshold = .5f;
    double trainAccuracy = eval(split.train, bestThreshold).accuracy();
    for (int step = 20; step <= 80; ++step) {
        const float threshold = float(step) / 100;
        const double accuracy = eval(split.train, threshold).accuracy();
        if (accuracy > trainAccuracy || (std::abs(accuracy - trainAccuracy) < 1e-9 &&
            std::abs(threshold - .5f) < std::abs(bestThreshold - .5f))) {
            trainAccuracy = accuracy;
            bestThreshold = threshold;
        }
    }
    const auto validation = eval(split.validation, bestThreshold);
    const auto general = eval(split.validation, .5f, true);
    const double bestAccuracy = validation.accuracy();
    const double baseline = general.accuracy();
    if (bestAccuracy < .65 || bestAccuracy < baseline ||
        validation.fn > general.fn || validation.fp > general.fp) {
        if (error) *error = "profilo rifiutato sulla registrazione separata: miss " +
            std::to_string(validation.fn) + " (generale " + std::to_string(general.fn) +
            "), falsi positivi " + std::to_string(validation.fp) + " (generale " +
            std::to_string(general.fp) + ")";
        return false;
    }
    candidate.validationMisses = validation.fn;
    candidate.validationFalsePositives = validation.fp;
    candidate.baselineMisses = general.fn;
    candidate.baselineFalsePositives = general.fp;
    candidate.validationSource = split.validationSource;

    candidate.threshold = bestThreshold;
    candidate.positives = positives;
    candidate.negatives = negatives;
    candidate.validationExamples = static_cast<int>(split.validation.size());
    candidate.validationBalancedAccuracy = bestAccuracy;
    candidate.baselineBalancedAccuracy = baseline;
    candidate.createdAt = timestamp();
    std::vector<std::string> sources;
    for (const LearningExample& example : examples) {
        sources.push_back(sourceKey(example));
    }
    std::sort(sources.begin(), sources.end());
    sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
    candidate.sources = sources;
    profile = candidate;
    if (error) error->clear();
    return true;
}

bool savePersonalProfile(const std::string& path, const PersonalProfile& profile,
                         std::string* error) {
    std::error_code directoryError;
    const std::filesystem::path fsPath = std::filesystem::u8path(path);
    if (!fsPath.parent_path().empty()) {
        std::filesystem::create_directories(fsPath.parent_path(), directoryError);
    }
    std::ostringstream out;
    out << std::setprecision(9);
    out << "schema=2\n"
        << "valid=" << (profile.valid ? 1 : 0) << "\n"
        << "bias=" << profile.bias << "\n"
        << "weightScore=" << profile.weightScore << "\n"
        << "weightDuration=" << profile.weightDuration << "\n"
        << "weightRms=" << profile.weightRms << "\n"
        << "weightZcr=" << profile.weightZcr << "\n"
        << "weightMean=" << profile.weightMean << "\n"
        << "threshold=" << profile.threshold << "\n"
        << "positives=" << profile.positives << "\n"
        << "negatives=" << profile.negatives << "\n"
        << "validationExamples=" << profile.validationExamples << "\n"
        << "validationBalancedAccuracy=" << profile.validationBalancedAccuracy << "\n"
        << "baselineBalancedAccuracy=" << profile.baselineBalancedAccuracy << "\n"
        << "validationMisses=" << profile.validationMisses << "\n"
        << "validationFalsePositives=" << profile.validationFalsePositives << "\n"
        << "baselineMisses=" << profile.baselineMisses << "\n"
        << "baselineFalsePositives=" << profile.baselineFalsePositives << "\n"
        << "validationSource=" << profile.validationSource << "\n"
        << "createdAt=" << profile.createdAt << "\n";
    for (const std::string& source : profile.sources) {
        out << "source=" << source << "\n";
    }
    return writeTextAtomic(path, out.str(), error);
}

bool loadPersonalProfile(const std::string& path, PersonalProfile& profile,
                         std::string* error) {
    std::ifstream in(nativeFilePath(std::filesystem::u8path(path)), std::ios::binary);
    if (!in) {
        if (error) *error = "profilo personale non trovato";
        return false;
    }
    std::map<std::string, std::string> values;
    std::vector<std::string> sources;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "source") sources.push_back(value);
        else values[key] = value;
    }
    bool invalidNumber = false;
    auto number = [&](const char* key, float fallback) {
        const auto it = values.find(key);
        if (it == values.end()) return fallback;
        try {
            size_t consumed = 0;
            const float value = std::stof(it->second, &consumed);
            if (consumed == it->second.size() && std::isfinite(value) && std::abs(value) <= 10000000) return value;
        } catch (...) {}
        invalidNumber = true;
        return fallback;
    };
    if (values["schema"] != "2") {
        if (error) *error = "profilo precedente da ricalibrare: validazione non indipendente";
        return false;
    }
    PersonalProfile loaded;
    loaded.validationSource = values["validationSource"];
    loaded.validationMisses = static_cast<int>(number("validationMisses", 0));
    loaded.validationFalsePositives = static_cast<int>(number("validationFalsePositives", 0));
    loaded.baselineMisses = static_cast<int>(number("baselineMisses", 0));
    loaded.baselineFalsePositives = static_cast<int>(number("baselineFalsePositives", 0));
    loaded.valid = number("valid", 0.0f) != 0.0f;
    loaded.bias = number("bias", 0.0f);
    loaded.weightScore = number("weightScore", 0.0f);
    loaded.weightDuration = number("weightDuration", 0.0f);
    loaded.weightRms = number("weightRms", 0.0f);
    loaded.weightZcr = number("weightZcr", 0.0f);
    loaded.weightMean = number("weightMean", 0.0f);
    loaded.threshold = number("threshold", 0.5f);
    loaded.positives = static_cast<int>(number("positives", 0.0f));
    loaded.negatives = static_cast<int>(number("negatives", 0.0f));
    loaded.validationExamples = static_cast<int>(number("validationExamples", 0.0f));
    loaded.validationBalancedAccuracy = number("validationBalancedAccuracy", 0.0f);
    loaded.baselineBalancedAccuracy = number("baselineBalancedAccuracy", 0.0f);
    const auto created = values.find("createdAt");
    if (created != values.end()) loaded.createdAt = created->second;
    loaded.sources = std::move(sources);
    if (invalidNumber || !loaded.valid || loaded.positives < 6 || loaded.negatives < 6 ||
        loaded.sources.size() < 2 || loaded.validationBalancedAccuracy < .65 ||
        loaded.validationBalancedAccuracy > 1 || loaded.baselineBalancedAccuracy < 0 ||
        loaded.baselineBalancedAccuracy > 1 || loaded.validationMisses < 0 || loaded.validationFalsePositives < 0 ||
        loaded.baselineMisses < 0 || loaded.baselineFalsePositives < 0 ||
        !std::isfinite(loaded.bias) || !std::isfinite(loaded.weightScore) ||
        !std::isfinite(loaded.weightDuration) || !std::isfinite(loaded.weightRms) ||
        !std::isfinite(loaded.weightZcr) || !std::isfinite(loaded.weightMean) ||
        !std::isfinite(loaded.threshold) || loaded.threshold <= 0 || loaded.threshold >= 1 ||
        loaded.validationExamples < 4 || loaded.validationSource.empty()) {
        if (error) *error = "il file non contiene un profilo valido";
        return false;
    }
    profile = loaded;
    if (error) error->clear();
    return true;
}

} // namespace bc
