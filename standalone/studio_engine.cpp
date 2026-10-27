// SPDX-License-Identifier: MPL-2.0
#include "studio_engine.h"
#ifndef _WIN32
#include <openssl/evp.h>
#endif

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <fstream>
#include <set>

#include "core/detect/offline_analyzer.h"
#include "core/detect/detector_settings_codec.h"
#include "core/util/atomic_file.h"
#include "core/util/json_validation.h"
#include "respiro_onnx.h"
#include "core/edit/region_editor.h"
#include "core/edit/audio_io.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace bc {
namespace studio {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string jsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default: out += c; break;
        }
    }
    return out;
}

void writeClickDiagnostics(std::ostream& out,const ClickRepairDiagnostics& d) {
    out << "{\"requested\": \"" << jsonEscape(d.requestedMethod)
            << "\", \"selected\": \"" << jsonEscape(d.selectedMethod)
            << "\", \"reason\": \"" << jsonEscape(d.reason)
            << "\", \"duration_ms\": " << d.durationMs
            << ", \"context_ms\": " << d.contextMs
            << ", \"linear_validation_error\": " << d.linearValidationError
            << ", \"linear_voice_risk\": " << d.linearVoiceRisk
            << ", \"predictive_bridge_error\": " << d.predictiveBridgeError
            << ", \"predictive_voice_risk\": " << d.predictiveVoiceRisk
            << ", \"boundary_level_ratio\": " << d.boundaryLevelRatio
            << ", \"predictive_validation_error\": " << d.predictiveValidationError
            << ", \"prediction_disagreement\": " << d.predictionDisagreement
            << ", \"impulse_ratio\": " << d.impulseRatio
            << ", \"context_high_band_ratio\": " << d.contextHighBandRatio
            << ", \"context_level_ratio\": " << d.contextLevelRatio << "}";
}

std::string formatDb(double db) {
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "%.2f", db);
    return buffer;
}

std::string baseName(const std::string& path) {
    return std::filesystem::path(path).stem().string();
}

std::string formatName(WavFormat format) {
    switch (format) {
        case WavFormat::Pcm16: return "pcm16";
        case WavFormat::Pcm24: return "pcm24";
        case WavFormat::Float32: return "float32";
    }
    return "?";
}

WavFormat chooseFormat(const WavInfo& info) {
    if (info.isFloat || info.bitsPerSample == 32) return WavFormat::Float32;
    if (info.bitsPerSample <= 16) return WavFormat::Pcm16;
    return WavFormat::Pcm24;
}

WavFormat chooseFormat(const AudioFileInfo& info) {
    if (info.isFloat || info.bitsPerSample == 32) return WavFormat::Float32;
    if (info.bitsPerSample <= 16) return WavFormat::Pcm16;
    return WavFormat::Pcm24;
}

bool ensureDirectory(const std::string& path, std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(path), ec);
    if (ec) {
        if (error) *error = "impossibile creare la cartella di uscita: " + ec.message();
        return false;
    }
    return true;
}

double gainToDb(float gain) {
    return gain > 0.0f ? 20.0 * std::log10(static_cast<double>(gain)) : -160.0;
}

} // namespace

const char* editModeName(EditMode mode) {
    switch (mode) {
        case EditMode::Attenuate: return "attenuate";
        case EditMode::Cut: return "cut";
        case EditMode::Halve: return "halve";
        case EditMode::Silence: return "silence";
    }
    return "attenuate";
}

bool editModeFromName(const std::string& name, EditMode* mode) {
    if (!mode) return false;
    if (name == "attenuate" || name == "attenua") { *mode = EditMode::Attenuate; return true; }
    if (name == "cut" || name == "taglia") { *mode = EditMode::Cut; return true; }
    if (name == "halve" || name == "dimezza") { *mode = EditMode::Halve; return true; }
    if (name == "silence" || name == "silenzia") { *mode = EditMode::Silence; return true; }
    return false;
}

Event eventFromRegion(const EditRegion& r) {
    Event e;
    e.kind = r.kind; e.startSample = r.startSample; e.endSample = r.endSample;
    e.score = r.score; e.reductionDb = r.reductionDb; e.repair = r.repair;
    e.enabled = r.enabled; e.approved = r.approved;
    e.state = (r.learningLabel == "false_positive" || r.learningLabel == "click_false_positive" || r.learningLabel == "vocal_transient") ? "rejected" :
        (r.learningLabel == "breath_confirmed" || r.learningLabel == "missed_breath" || r.learningLabel == "click_confirmed" || r.learningLabel == "missed_click" || r.learningLabel == "mouth_click_confirmed") ? "confirmed" :
        (!r.enabled || !r.approved) ? "excluded" : r.fromDetection ? "proposed" : "manual";
    e.mode = static_cast<EditMode>(std::clamp(r.editMode, 0, 3));
    e.keepFraction = r.keepFraction; e.fadeMs = r.fadeMs; e.safetyMarginMs = r.safetyMarginMs;
    e.decisionSource = r.decisionSource; e.classification = r.classification.empty() && !r.fromDetection ? "manual" : r.classification;
    e.trainingEligible = r.trainingEligible; e.learningLabel = r.learningLabel; e.repairMethod = r.repairMethod;
    e.repairFeedback=r.repairFeedback; e.repairFeedbackMethod=r.repairFeedbackMethod;
    e.source = e.classification; e.reason = r.reason;
    e.dspScore = r.dspScore; e.aiScore = r.aiScore; e.aiMaxProbability = r.aiMaxProbability;
    e.manualOperation = r.manualOperation;
    return e;
}

Detection detectionFromEvent(const Event& e) {
    Detection d;
    d.kind = e.kind; d.startSample = e.startSample; d.endSample = e.endSample; d.peakScore = e.score;
    d.classification = e.classification; d.reason = e.reason;
    d.dspScore = e.dspScore; d.aiScore = e.aiScore; d.aiMaxProbability = e.aiMaxProbability;
    return d;
}

int64_t TimeMap::mapOriginal(int64_t sample, bool* removed) const {
    if (removed) *removed = false;
    if (originalFrames <= 0) return 0;
    sample = std::max<int64_t>(0, std::min<int64_t>(originalFrames, sample));
    for (const auto& segment : segments) {
        if (sample >= segment.originalStart && sample < segment.originalEnd) {
            return segment.editedStart + (sample - segment.originalStart);
        }
    }
    // In un buco: si aggancia al bordo conservato piu' vicino a sinistra.
    if (removed) *removed = true;
    int64_t best = 0;
    for (const auto& segment : segments) {
        if (segment.originalEnd <= sample) best = std::max(best, segment.editedEnd);
    }
    return best;
}

int64_t TimeMap::mapEdited(int64_t sample, bool* gap) const {
    if (gap) *gap = false;
    if (editedFrames <= 0) return 0;
    sample = std::max<int64_t>(0, std::min<int64_t>(editedFrames, sample));
    for (const auto& segment : segments) {
        if (sample >= segment.editedStart && sample < segment.editedEnd) {
            return segment.originalStart + (sample - segment.editedStart);
        }
    }
    if (gap) *gap = true;
    int64_t best = 0;
    for (const auto& segment : segments) {
        if (segment.editedEnd <= sample) best = std::max(best, segment.originalEnd);
    }
    return best;
}

bool TimeMap::isRemoved(int64_t start, int64_t end) const {
    if (end <= start) return false;
    int64_t covered = 0;
    for (const auto& segment : segments) {
        const int64_t from = std::max(start, segment.originalStart);
        const int64_t to = std::min(end, segment.originalEnd);
        if (to > from) covered += to - from;
    }
    return covered < (end - start);
}


bool sha256File(const std::string& path, std::string& hex, std::string* error) {
#ifdef _WIN32
    std::ifstream file(nativeFilePath(std::filesystem::u8path(path)), std::ios::binary);
    if (!file) {
        if (error) *error = "file non apribile per SHA-256";
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> object;
    std::vector<unsigned char> digest(32);
    DWORD objectSize = 0;
    DWORD resultSize = 0;
    bool ok = false;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        if (error) *error = "BCrypt non disponibile per SHA-256";
        return false;
    }
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&objectSize),
                          sizeof(objectSize), &resultSize, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        if (error) *error = "BCrypt: dimensione oggetto non disponibile";
        return false;
    }
    object.resize(objectSize);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize,
                         nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        if (error) *error = "BCrypt: creazione hash fallita";
        return false;
    }

    std::vector<char> buffer(1u << 20);
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = file.gcount();
        if (got > 0 &&
            BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                           static_cast<ULONG>(got), 0) < 0) {
            break;
        }
    }
    if (file.bad()) {
        if (error) *error = "errore di lettura durante SHA-256";
    } else if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0) {
        std::ostringstream stream;
        stream << std::hex << std::setfill('0');
        for (unsigned char byte : digest) stream << std::setw(2) << static_cast<int>(byte);
        hex = stream.str();
        ok = true;
    } else if (error) {
        *error = "BCrypt: chiusura hash fallita";
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
#else
    std::ifstream file(std::filesystem::u8path(path),std::ios::binary);
    if(!file){if(error)*error="file non apribile per SHA-256";return false;}
    auto* context=EVP_MD_CTX_new();
    if(!context){if(error)*error="SHA-256 allocation failed";return false;}
    bool ok=EVP_DigestInit_ex(context,EVP_sha256(),nullptr)==1;
    std::vector<char> buffer(1u<<20);
    while(ok && file){file.read(buffer.data(),buffer.size());auto count=file.gcount();if(count>0)ok=EVP_DigestUpdate(context,buffer.data(),static_cast<size_t>(count))==1;}
    unsigned char digest[EVP_MAX_MD_SIZE];unsigned int length=0;
    ok=ok && !file.bad() && EVP_DigestFinal_ex(context,digest,&length)==1 && length==32;
    EVP_MD_CTX_free(context);
    if(!ok){if(error)*error="SHA-256 read/digest failed";return false;}
    std::ostringstream stream;stream<<std::hex<<std::setfill('0');
    for(unsigned int i=0;i<length;++i)stream<<std::setw(2)<<static_cast<int>(digest[i]);
    hex=stream.str();return true;
#endif
}

std::vector<Event> fragmentedBreathReviewProposals(const bc::AnalysisResult& f,
                                                  const std::vector<float>& ai, float aiThreshold) {
    std::vector<Event> out;
    if (f.hopSize<=0 || !std::isfinite(f.sampleRate) || f.sampleRate<=0 || f.totalSamples<=0 ||
        !std::isfinite(aiThreshold) || aiThreshold<0 || aiThreshold>1 || f.breathScores.empty() || ai.empty() ||
        f.frameLevelDb.size()!=f.breathScores.size()) return out;
    struct Run {size_t first,last,count,active;float peak;};
    std::vector<Run> groups;int64_t start=-1;float peak=0;
    const float enter=f.settings.breathThreshold,exit=enter*.6f;
    for (size_t i=0;i<=f.breathScores.size();++i) {
        const float score=i<f.breathScores.size()?f.breathScores[i]:-1.f;
        if (start<0) {if(score>=enter){start=static_cast<int64_t>(i);peak=score;}}
        else if(score>=exit) peak=std::max(peak,score);
        else {
            Run r{static_cast<size_t>(start),i-1,1,i-static_cast<size_t>(start),peak};
            if(!groups.empty() && (r.first-groups.back().last-1)*f.hopSize<=f.sampleRate*.01) {
                auto& g=groups.back();g.last=r.last;g.count++;g.active+=r.active;g.peak=std::max(g.peak,r.peak);
            } else groups.push_back(r);
            start=-1;
        }
    }
    for(const auto& g:groups) {
        const int64_t a=static_cast<int64_t>(g.first)*f.hopSize;
        const int64_t b=std::min(f.totalSamples,static_cast<int64_t>(g.last)*f.hopSize+std::max(128,f.settings.fftSize));
        if(a<f.sampleRate*.03 || b>f.totalSamples-f.sampleRate*.03 ||
            g.count<2 || g.active*f.hopSize<f.sampleRate*f.settings.minBreathMs/1000. ||
            b<=a || b-a>f.sampleRate*std::min(400,f.settings.maxBreathMs)/1000.) continue;
        double sum=0;size_t n=0;std::vector<float> neighbours;
        for(size_t i=g.first;i<=g.last;++i) if(f.frameLevelDb[i]>-150){sum+=f.frameLevelDb[i];++n;}
        const size_t radius=static_cast<size_t>(f.sampleRate*.25/f.hopSize);
        for(size_t i=g.first>radius?g.first-radius:0;i<std::min(f.frameLevelDb.size(),g.last+radius+1);++i)
            if((i<g.first||i>g.last)&&f.frameLevelDb[i]>-150)neighbours.push_back(f.frameLevelDb[i]);
        if(!n)continue;
        if(f.settings.requirePauseForBreath && !neighbours.empty()) {
            auto mid=neighbours.begin()+neighbours.size()/2;std::nth_element(neighbours.begin(),mid,neighbours.end());
            if(*mid>sum/n+3+(f.settings.breathSensitivity-.5f)*12)continue;
        }
        float aiPeak=0;
        for(size_t i=static_cast<size_t>(a/f.sampleRate*100);i<std::min(ai.size(),static_cast<size_t>(std::ceil(b/f.sampleRate*100)));++i)
            aiPeak=std::max(aiPeak,ai[i]);
        if(aiPeak<=aiThreshold)continue;
        Event e;e.startSample=a;e.endSample=b;e.score=e.dspScore=g.peak;e.aiMaxProbability=aiPeak;
        e.source="dsp";e.classification="dsp_fragment_review";
        e.reason="Frammenti respiratori DSP con evidenza AI: rivedere; nessun trattamento automatico";
        out.push_back(e);
    }
    return out;
}

bool analyzeFile(const std::string& path, const Settings& settings,
                 Analysis* analysis, std::string* error, const AnalysisControl* control) {
    if (!analysis) {
        if (error) *error = "analisi nulla";
        return false;
    }

    const auto cancelled = [&] { return control && control->cancelled(error); };
    const auto progress = [&](float fraction, const char* stage) {
        if (control) control->report(fraction, stage);
    };
    if (cancelled()) return false;
    progress(-1, "Lettura della registrazione");
    if (cancelled()) return false;
    AudioBuffer buffer;
    AudioFileInfo info;
    if (!readAudio(path, buffer, &info, error)) return false;
    if (cancelled()) return false;
    if (buffer.empty()) {
        if (error) *error = "file audio vuoto";
        return false;
    }

    // Publish only a complete result. Cancellation and errors leave the host's
    // previous analysis intact, including its manual decisions and labels.
    Analysis completed;
    Analysis* destination = analysis;
    analysis = &completed;
    analysis->inputPath = path;
    analysis->inputSha256.clear();
    std::string hashError;
    progress(-1, "Verifica della registrazione");
    if (cancelled()) return false;
    if (!sha256File(path, analysis->inputSha256, &hashError)) {
        analysis->inputSha256 = "non-calcolato: " + hashError;
    }
    if (cancelled()) return false;
    analysis->sampleRate = buffer.sampleRate;
    analysis->channels = buffer.channels;
    analysis->bitsPerSample = info.bitsPerSample;
    analysis->isFloat = info.isFloat;
    analysis->frames = buffer.frameCount();
    analysis->durationSeconds = buffer.durationSeconds();
    std::string detectorMode = settings.detector;
    if (settings.useRespiroEn && detectorMode == "dsp") detectorMode = "fusion";
    if (detectorMode != "dsp" && detectorMode != "ai" && detectorMode != "fusion") {
        detectorMode = "dsp";
    }
    analysis->detector = detectorMode;
    analysis->events.clear();
    analysis->excludedEvents.clear();
    analysis->aiInferenceFrames = 0;
    analysis->aiProbabilities.clear();

    const std::vector<float> mono = buffer.toMono();
    if (cancelled()) return false;
    const double preFrames = settings.prePadMs * buffer.sampleRate / 1000.0;
    const double postFrames = settings.postPadMs * buffer.sampleRate / 1000.0;

    auto clampEvent = [&](int64_t start, int64_t end) {
        start = std::max<int64_t>(0, start);
        end = std::min<int64_t>(buffer.frameCount(), end);
        return std::make_pair(start, end);
    };

    const bool breaths = settings.dspSettings ? settings.dspSettings->detectBreath : settings.detectBreath;
    const bool clicks = settings.dspSettings ? settings.dspSettings->detectClicks : settings.detectClicks;
    if (!breaths && !clicks) { if (error) *error = "Seleziona Rileva respiri oppure Rileva click"; return false; }
    std::vector<Event> dspEvents, clickEvents;
    bc::AnalysisResult dspFeatures;
    if ((breaths && (detectorMode == "dsp" || detectorMode == "fusion")) || clicks) {
        DetectorSettings detector;
        if (settings.dspSettings) detector = *settings.dspSettings;
        else {
            detector.detectBreath = breaths;
            detector.detectClicks = clicks;
            detector.clickSensitivity = settings.clickSensitivity;
            detector.clickThreshold = settings.clickThreshold;
            detector.breathSensitivity = settings.sensitivity;
            detector.breathThreshold = settings.threshold;
        }

        detector.detectBreath = breaths && detectorMode != "ai";
        detector.detectClicks = clicks;
        detector.includeScores = detector.includeScores || (breaths && detectorMode=="fusion");
        OfflineAnalyzer analyzer;
        analyzer.setCancellationCallback(cancelled);
        analyzer.setProgressCallback(progress);
        AnalysisResult result = analyzer.analyze(
            mono.data(), mono.size(), static_cast<double>(buffer.sampleRate), detector);
        if (cancelled()) return false;
        if (!result.error.empty()) {
            if (error) *error = result.error;
            return false;
        }
        for (const Detection& detection : result.detections) {

            int64_t start = static_cast<int64_t>(std::llround(
                static_cast<double>(detection.startSample) - preFrames));
            int64_t end = static_cast<int64_t>(std::llround(
                static_cast<double>(detection.endSample) + postFrames));
            if (detection.kind == DetectionKind::Click) { start = detection.startSample; end = detection.endSample; }
            auto bounds = clampEvent(start, end);
            if (bounds.second - bounds.first < (detection.kind == DetectionKind::Click ? 1 :
                static_cast<int64_t>(buffer.sampleRate * 0.02))) {
                continue;
            }
            Event event;
            event.kind = detection.kind;
            event.repair = detection.kind == DetectionKind::Click;
            event.startSample = bounds.first;
            event.endSample = bounds.second;
            event.score = detection.peakScore;
            event.enabled = true;
            event.state = "proposed";
            event.source = "dsp";
            event.dspScore = detection.peakScore;
            event.classification = detection.classification;
            event.reason = detection.reason;
            if (event.kind == DetectionKind::Click) clickEvents.push_back(event);
            else dspEvents.push_back(event);
        }
        if(breaths && detectorMode=="fusion")dspFeatures=std::move(result);
    }

    std::vector<Event> aiEvents;
    std::vector<float> aiProbabilities;
    if (breaths && (detectorMode == "ai" || detectorMode == "fusion")) {
        std::vector<AiEvent> detected;
        std::string aiError;
        if (!respiroEnDetect(mono, buffer.sampleRate, settings.modelPath,
                             settings.aiThreshold, &detected, &aiProbabilities, &aiError, control)) {
            if (cancelled()) return false;
            if (error) *error = "Respiro-en: " + aiError;
            return false;
        }
        if (cancelled()) return false;
        analysis->aiInferenceFrames = static_cast<int64_t>(aiProbabilities.size());
        for (const AiEvent& ai : detected) {
            int64_t start = static_cast<int64_t>(std::llround(
                static_cast<double>(ai.startFrame) * 160.0 * buffer.sampleRate / 16000.0 -
                preFrames));
            int64_t end = static_cast<int64_t>(std::llround(
                static_cast<double>(ai.endFrame) * 160.0 * buffer.sampleRate / 16000.0 +
                postFrames));
            auto bounds = clampEvent(start, end);
            if (bounds.second - bounds.first < static_cast<int64_t>(buffer.sampleRate * 0.02)) {
                continue;
            }
            Event event;
            event.startSample = bounds.first;
            event.endSample = bounds.second;
            event.score = ai.score;
            event.enabled = true;
            event.state = "ai";
            event.source = "ai";
            event.aiScore = ai.score;
            event.aiMaxProbability = ai.score;
            aiEvents.push_back(event);
        }
    }

    progress(-1, "Preparazione delle proposte");
    if (cancelled()) return false;
    if (detectorMode == "dsp") {
        analysis->events = std::move(dspEvents);
    } else if (detectorMode == "ai") {
        analysis->events = std::move(aiEvents);
    } else {
        // Fusione tracciabile: ogni evento dichiara se viene da AI, DSP o da
        // entrambi. Un candidato solo-DSP entra solo con supporto AI minimo;
        // altrimenti finisce in excludedEvents con il motivo.
        analysis->events = aiEvents;
        for (const Event& sourceCandidate : dspEvents) {
            if (cancelled()) return false;
            Event candidate = sourceCandidate;
            int sharedIndex = -1;
            int64_t bestOverlap = 0;
            for (size_t i = 0; i < analysis->events.size(); ++i) {
                const Event& accepted = analysis->events[i];
                const int64_t from = std::max(candidate.startSample, accepted.startSample);
                const int64_t to = std::min(candidate.endSample, accepted.endSample);
                if (to <= from) continue;
                const int64_t overlap = to - from;
                const int64_t smaller = std::min(candidate.endSample - candidate.startSample,
                                                 accepted.endSample - accepted.startSample);
                if (smaller > 0 && overlap * 2 >= smaller && overlap > bestOverlap) {
                    bestOverlap = overlap;
                    sharedIndex = static_cast<int>(i);
                }
            }
            if (sharedIndex >= 0) {
                Event& shared = analysis->events[static_cast<size_t>(sharedIndex)];
                shared.source = "shared";
                shared.dspScore = candidate.score;
                shared.aiScore = shared.aiScore > 0.0f ? shared.aiScore : shared.score;
                shared.reason = "proposta condivisa DSP + Respiro-en";
                continue;
            }
            float maxProbability = 0.0f;
            if (!aiProbabilities.empty()) {
                const int64_t startFrame = candidate.startSample * 100 / buffer.sampleRate;
                const int64_t endFrame = candidate.endSample * 100 / buffer.sampleRate;
                for (int64_t frame = std::max<int64_t>(0, startFrame);
                     frame < std::min<int64_t>(static_cast<int64_t>(aiProbabilities.size()), endFrame);
                     ++frame) {
                    maxProbability = std::max(maxProbability,
                        aiProbabilities[static_cast<size_t>(frame)]);
                }
            }
            candidate.aiMaxProbability = maxProbability;
            if (!aiProbabilities.empty() && maxProbability < settings.fusionAiFloor) {
                Event excluded = candidate;
                excluded.enabled = false;
                excluded.approved = false;
                excluded.state = "excluded";
                char reason[256] = {};
                std::snprintf(reason, sizeof(reason),
                    "candidato solo-DSP escluso: probabilita' AI massima %.4f < soglia %.4f",
                    maxProbability, settings.fusionAiFloor);
                excluded.reason = reason;
                analysis->excludedEvents.push_back(excluded);
            } else {
                candidate.source = "dsp";
                char reason[256] = {};
                std::snprintf(reason, sizeof(reason),
                    "candidato solo-DSP accettato: probabilita' AI massima %.4f >= soglia %.4f",
                    maxProbability, settings.fusionAiFloor);
                candidate.reason = reason;
                analysis->events.push_back(candidate);
            }
        }
    }

    for (Event& event : analysis->events) {
        event.classification = event.source.empty() ? event.state : event.source;
        event.decisionSource = "automatic";
        event.trainingEligible = false;
        if (event.source == "ai") {
            event.reason = "proposta solo Respiro-en";
        } else if (event.source.empty()) {
            event.source = detectorMode == "ai" ? "ai" : "dsp";
            event.reason = detectorMode == "ai" ? "proposta Respiro-en" : "proposta DSP";
        }
    }
    // Preserve all accepted Fusion events. Short AI runs are separate review
    // proposals, never a reason to expand or automatically approve an event.
    for (const auto& shortEvent : shortBreathReviewProposals(aiProbabilities, settings.aiThreshold)) {
        auto bounds=clampEvent(static_cast<int64_t>(std::llround(shortEvent.startFrame*.01*buffer.sampleRate-preFrames)),
                               static_cast<int64_t>(std::llround(shortEvent.endFrame*.01*buffer.sampleRate+postFrames)));
        if (std::any_of(analysis->events.begin(),analysis->events.end(),[&](const Event& e) {
            return e.kind==DetectionKind::Breath && e.startSample<bounds.second && e.endSample>bounds.first;
        })) continue;
        Event e; e.startSample=bounds.first;e.endSample=bounds.second;e.score=e.aiScore=e.aiMaxProbability=shortEvent.score;
        e.source="ai";e.classification="ai_short_review";
        e.reason="Respiro AI breve (80–200 ms): rivedere; nessun trattamento automatico";
        analysis->events.push_back(e);
    }
    for(auto e:fragmentedBreathReviewProposals(dspFeatures,aiProbabilities,std::max(settings.aiThreshold,settings.fusionAiFloor))) {
        auto bounds=clampEvent(static_cast<int64_t>(std::llround(e.startSample-preFrames)),static_cast<int64_t>(std::llround(e.endSample+postFrames)));
        e.startSample=bounds.first;e.endSample=bounds.second;
        if(std::none_of(analysis->events.begin(),analysis->events.end(),[&](const Event& old){
            return old.kind==DetectionKind::Breath && old.startSample<e.endSample && old.endSample>e.startSample;
        }))analysis->events.push_back(e);
    }
    analysis->events.insert(analysis->events.end(), clickEvents.begin(), clickEvents.end());
    std::sort(analysis->events.begin(), analysis->events.end(),
              [](const Event& a, const Event& b) { return a.startSample < b.startSample; });

    for (Event& event : analysis->events) {
        auto bounds = clampEvent(event.startSample, event.endSample);
        event.startSample = bounds.first;
        event.endSample = bounds.second;
        if (event.kind == DetectionKind::Breath && event.endSample - event.startSample <
            static_cast<int64_t>(buffer.sampleRate * 0.02)) {
            event.startSample = event.endSample; // verra' filtrato sotto
        }
    }
    analysis->events.erase(
        std::remove_if(analysis->events.begin(), analysis->events.end(),
                       [](const Event& e) { return e.endSample <= e.startSample; }),
        analysis->events.end());

    if (settings.traceAiProbabilities) analysis->aiProbabilities=std::move(aiProbabilities);
    if (cancelled()) return false;
    progress(1, "Analisi completata");
    if (cancelled()) return false;
    *destination = std::move(completed);
    return true;
}

std::string renderPlanSignature(const EditPlan& p) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << std::quoted(p.sourcePath) << ' ' << p.sampleRate << ' ' << p.channels
        << ' ' << p.preserveDuration << ' ' << p.masterGainDb
        << ' ' << p.settings.reductionDb << ' ' << p.settings.fadeMs << ' ' << p.settings.crossfadeMs
        << ' ' << p.fade.inEnabled << ' ' << p.fade.inSeconds << ' ' << static_cast<int>(p.fade.inShape)
        << ' ' << p.fade.outEnabled << ' ' << p.fade.outSeconds << ' ' << static_cast<int>(p.fade.outShape);
    for (const auto& e : p.events) {
        if (!e.enabled || !e.approved) continue;
        out << '\n' << static_cast<int>(e.kind) << ' ' << e.startSample << ' ' << e.endSample
            << ' ' << static_cast<int>(e.mode) << ' ' << e.keepFraction << ' ' << e.fadeMs
            << ' ' << e.safetyMarginMs << ' ' << e.reductionDb.value_or(p.settings.reductionDb)
            << ' ' << e.repair << ' ' << std::quoted(e.repairMethod) << ' ' << e.manualOperation;
    }
    return out.str();
}

const char* operationOutcome(const EditOperationResult& op) {
    if (op.declined) return "preserved_safety";
    if (op.bypassed) return "bypassed";
    if (!op.applied) {
        if (!op.event.enabled || !op.event.approved)
            return op.event.decisionSource == "user" ? "excluded" : "review";
        return "not_applied";
    }
    if (op.event.kind == DetectionKind::Click && op.event.repair && op.event.mode == EditMode::Attenuate &&
        (op.clickRepair.selectedMethod == "linear" || op.clickRepair.selectedMethod == "predictive"))
        return "repaired";
    return "applied";
}

const char* eventOutcome(const Event& event, const EditResult* rendered) {
    if (!event.enabled || !event.approved)
        return event.decisionSource == "user" ? "excluded" : "review";
    if (rendered) for (const auto& op : rendered->operations) {
        if (op.event.kind == event.kind && op.event.startSample == event.startSample &&
            op.event.endSample == event.endSample && op.event.mode == event.mode &&
            op.event.manualOperation == event.manualOperation && op.event.repair == event.repair &&
            op.event.repairMethod == event.repairMethod) return operationOutcome(op);
    }
    return "pending";
}

AudioBuffer removedSignal(const AudioBuffer& original,const EditResult& edited) {
    AudioBuffer residual=original;
    for(int64_t frame=0;frame<original.frameCount();++frame){
        bool removed=false;const auto mapped=edited.map.mapOriginal(frame,&removed);
        for(int c=0;c<original.channels;++c){
            const auto index=static_cast<size_t>(frame*original.channels+c);
            if(!removed && mapped>=0 && mapped<edited.audio.frameCount())
                residual.samples[index]-=edited.audio.samples[static_cast<size_t>(mapped*original.channels+c)];
        }
    }
    return residual;
}

namespace {

struct SampleRange {
    int64_t start = 0;
    int64_t end = 0;
};

void subtractRange(std::vector<SampleRange>& ranges, int64_t start, int64_t end) {
    std::vector<SampleRange> next;
    for (const SampleRange& range : ranges) {
        if (end <= range.start || start >= range.end) {
            next.push_back(range);
            continue;
        }
        if (start > range.start) next.push_back({range.start, start});
        if (end < range.end) next.push_back({end, range.end});
    }
    ranges.swap(next);
}

void smoothJoin(AudioBuffer& audio, int64_t outputPosition,
                int64_t fadeFrames) {
    if (audio.channels <= 0 || fadeFrames <= 0) return;
    const int64_t frames = audio.frameCount();
    fadeFrames = std::min(fadeFrames, std::min(outputPosition, frames - outputPosition));
    if (fadeFrames <= 1) return;
    for (int64_t i = 0; i < fadeFrames; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(fadeFrames - 1);
        const float leftGain = static_cast<float>(0.5 * (1.0 + std::cos(kPi * t)));
        const float rightGain = static_cast<float>(0.5 * (1.0 - std::cos(kPi * t)));
        const int64_t leftFrame = outputPosition - fadeFrames + i;
        const int64_t rightFrame = outputPosition + i;
        for (int channel = 0; channel < audio.channels; ++channel) {
            audio.samples[static_cast<size_t>(leftFrame) * audio.channels + channel] *= leftGain;
            audio.samples[static_cast<size_t>(rightFrame) * audio.channels + channel] *= rightGain;
        }
    }
}

void applyEventGain(AudioBuffer& audio, const Event& event, float reductionDb,
                    std::vector<float>* curve = nullptr) {
    const int64_t frames = audio.frameCount();
    const int64_t start = std::max<int64_t>(0, event.startSample);
    const int64_t end = std::min<int64_t>(frames, event.endSample);
    if (end <= start) return;
    const int64_t fade = std::max<int64_t>(0, static_cast<int64_t>(
        std::llround(event.fadeMs * audio.sampleRate / 1000.0)));
    const float factor = event.mode == EditMode::Silence ? 0 : std::pow(10.0f, reductionDb / 20.0f);
    const RangeGainEnvelope envelope(start, end, fade, factor);
    for (int64_t frame = start; frame < end; ++frame) {
        const float gain = envelope.gainAt(frame);
        if (curve) { (*curve)[static_cast<size_t>(frame)] = std::min((*curve)[static_cast<size_t>(frame)], gain); continue; }
        for (int channel = 0; channel < audio.channels; ++channel) {
            audio.samples[static_cast<size_t>(frame) * audio.channels + channel] *= gain;
        }
    }
}

struct PlannedRemoval {
    bool valid = false;
    int64_t removeStart = 0;
    int64_t removeEnd = 0;
    int64_t marginFrames = 0;
    int64_t coreSamples = 0;
    std::string note;
};

PlannedRemoval planRemoval(const Event& event, int sampleRate) {
    PlannedRemoval removal;
    const int64_t length = event.endSample - event.startSample;
    if (length <= 0) {
        removal.note = "intervallo vuoto";
        return removal;
    }
    const int64_t margin = std::max<int64_t>(0, static_cast<int64_t>(
        std::llround(event.safetyMarginMs * sampleRate / 1000.0)));
    const int64_t effectiveMargin = std::min<int64_t>(margin, length / 2);
    const int64_t coreStart = event.startSample + effectiveMargin;
    const int64_t coreEnd = event.endSample - effectiveMargin;
    const int64_t coreLength = coreEnd - coreStart;
    removal.marginFrames = effectiveMargin;
    removal.coreSamples = std::max<int64_t>(0, coreLength);
    if (coreLength <= 0) {
        removal.note = "respiro piu' breve dei margini di sicurezza: nessun taglio";
        return removal;
    }
    if (event.mode == EditMode::Cut) {
        removal.removeStart = coreStart;
        removal.removeEnd = coreEnd;
        removal.valid = true;
        return removal;
    }
    if (event.mode == EditMode::Halve) {
        const double fraction = std::max(0.0, std::min(1.0, event.keepFraction));
        int64_t removeLength = static_cast<int64_t>(
            std::llround(static_cast<double>(coreLength) * (1.0 - fraction)));
        removeLength = std::max<int64_t>(0, std::min<int64_t>(coreLength, removeLength));
        if (removeLength <= 0) {
            removal.note = "percentuale conservata 100%: nessun taglio";
            return removal;
        }
        removal.removeStart = coreStart + (coreLength - removeLength) / 2;
        removal.removeEnd = removal.removeStart + removeLength;
        removal.valid = true;
        return removal;
    }
    return removal;
}

} // namespace

bool applyEditPlan(const AudioBuffer& original, const EditPlan& plan,
                   EditResult* result, std::string* error) {
    if (!result) {
        if (error) *error = "risultato nullo";
        return false;
    }
    if (original.empty()) {
        if (error) *error = "audio sorgente vuoto";
        return false;
    }
    if (plan.sampleRate > 0 && plan.sampleRate != original.sampleRate) {
        if (error) *error = "sample rate del piano diverso dal WAV";
        return false;
    }
    if (plan.channels > 0 && plan.channels != original.channels) {
        if (error) *error = "numero canali del piano diverso dal WAV";
        return false;
    }

    if (!std::isfinite(plan.settings.reductionDb) || plan.settings.reductionDb > 0 ||
        !std::isfinite(plan.settings.fadeMs) || plan.settings.fadeMs < 0 || plan.settings.fadeMs > 60000 ||
        !std::isfinite(plan.settings.crossfadeMs) || plan.settings.crossfadeMs < 0 || plan.settings.crossfadeMs > 60000) {
        if (error) *error = "parametri del piano non validi";
        return false;
    }
    for (const auto& event : plan.events) {
        if (!std::isfinite(event.keepFraction) || !std::isfinite(event.fadeMs) || event.fadeMs < 0 ||
            event.fadeMs > 60000 || !std::isfinite(event.safetyMarginMs) || event.safetyMarginMs < 0 ||
            (event.reductionDb && (!std::isfinite(*event.reductionDb) || *event.reductionDb > 0))) {
            if (error) *error = "parametri evento non validi";
            return false;
        }
    }
    if (!std::isfinite(plan.masterGainDb) || plan.masterGainDb > 0 ||
        !std::isfinite(plan.fade.inSeconds) || plan.fade.inSeconds < 0 || plan.fade.inSeconds > 3600 ||
        !std::isfinite(plan.fade.outSeconds) || plan.fade.outSeconds < 0 || plan.fade.outSeconds > 3600) {
        if (error) *error = "guadagno o fade del documento non validi";
        return false;
    }
    result->audio = original;
    result->map.originalFrames = original.frameCount();
    result->markers = plan.markers;
    result->mappedMarkers.clear();
    result->operations.clear();
    result->removedSamples = 0;
    result->crossfadeFrames = 0;
    result->appliedEvents = 0;
    result->rejectedEvents = 0;
    result->declinedEvents = 0;
    result->markerScope = plan.markerScope.empty() ? "voice" : plan.markerScope;
    result->voiceMappedMarkers.clear();
    result->projectMappedMarkers.clear();

    std::vector<Event> approved;
    for (const Event& event : plan.events) {
        if (!event.enabled || !event.approved) continue;
        if(event.repair && event.kind==DetectionKind::Click &&
           (event.repairMethod!="auto" && event.repairMethod!="linear" && event.repairMethod!="predictive" && event.repairMethod!="none")) {
            if(error)*error="Algoritmo de-click non riconosciuto";return false;
        }
        if(event.repair && event.kind==DetectionKind::Click && event.repairMethod!="none" && event.endSample-event.startSample>original.sampleRate*.045) {
            if(error)*error="Regione de-click oltre 45 ms: correggi i confini prima di riparare";return false;
        }
        if (event.startSample < 0 || event.endSample > original.frameCount() ||
            event.endSample <= event.startSample) {
            EditOperationResult rejected;
            rejected.event = event;
            rejected.note = "intervallo fuori dai limiti o vuoto";
            result->operations.push_back(rejected);
            ++result->rejectedEvents;
            continue;
        }
        if (event.mode == EditMode::Halve &&
            (event.keepFraction < 0.0 || event.keepFraction > 1.0)) {
            EditOperationResult rejected;
            rejected.event = event;
            rejected.note = "percentuale di dimezzamento non valida";
            result->operations.push_back(rejected);
            ++result->rejectedEvents;
            continue;
        }
        approved.push_back(event);
    }

    // Tagli e dimezzamenti: si applicano da destra a sinistra.
    std::vector<Event> timeEvents;
    for (const Event& event : approved) {
        if (event.kind == DetectionKind::Click && (event.mode == EditMode::Cut || event.mode == EditMode::Halve)) {
            if (error) *error = "I click non accorciano la durata: usa Ripara click o Silenzia";
            return false;
        }
        if (event.mode == EditMode::Cut || event.mode == EditMode::Halve) timeEvents.push_back(event);
    }
    std::sort(timeEvents.begin(), timeEvents.end(),
              [](const Event& a, const Event& b) { return a.startSample < b.startSample; });
    for (size_t i = 1; i < timeEvents.size(); ++i) {
        if (timeEvents[i].startSample < timeEvents[i - 1].endSample) {
            if (error) *error = "due interventi di timeline si sovrappongono";
            return false;
        }
    }

    std::vector<SampleRange> kept = {{0, original.frameCount()}};
    if (plan.preserveDuration) {
        // Silence the exact source range that compact mode would remove.
        // Keeping the retained portions in place avoids a second splice and
        // preserves every later cue. The shared gain renderer provides fades
        // inside the removed interval, including overlapping attenuations.
        EditPlan synchronized = plan;
        synchronized.preserveDuration = false;
        std::vector<std::pair<std::string, Event>> replaced;
        for (auto& event : synchronized.events) {
            if (!event.approved || !event.enabled || (event.mode != EditMode::Cut && event.mode != EditMode::Halve)) continue;
            if (event.startSample < 0 || event.endSample > original.frameCount() || event.endSample <= event.startSample) continue;
            const auto removal = planRemoval(event, original.sampleRate);
            if (!removal.valid) { if (error) *error = removal.note; return false; }
            const auto tag = "preserve_duration:" + std::to_string(replaced.size());
            replaced.push_back({tag,event});
            event.mode = EditMode::Silence;
            event.startSample = removal.removeStart;
            event.endSample = removal.removeEnd;
            event.reason = tag;
        }
        if (!applyEditPlan(original, synchronized, result, error)) return false;
        for (auto& op : result->operations) for (const auto& entry : replaced) {
            if (op.event.reason == entry.first) {
                op.event = entry.second;
                op.note = "mantieni sincronizzazione: intervallo ridotto sostituito da pausa";
            }
        }
        return true;
    }
    int64_t removedTotal = 0;
    // Le operazioni di timeline sono pianificate prima e applicate da destra
    // a sinistra: gli intervalli a sinistra mantengono le coordinate originali.
    for (auto it = timeEvents.rbegin(); it != timeEvents.rend(); ++it) {
        const PlannedRemoval removal = planRemoval(*it, original.sampleRate);
        if (removal.valid && removal.removeEnd > removal.removeStart) {
            subtractRange(kept, removal.removeStart, removal.removeEnd);
            removedTotal += removal.removeEnd - removal.removeStart;
        }
    }

    result->audio.samples.clear();
    result->audio.channels = original.channels;
    result->audio.sampleRate = original.sampleRate;
    result->map.segments.clear();
    int64_t outputFrames = 0;
    const int64_t maxFade = static_cast<int64_t>(std::llround(
        plan.settings.fadeMs * original.sampleRate / 1000.0));
    const int64_t requestedCrossfade = plan.settings.crossfadeMs > 0.0
        ? static_cast<int64_t>(std::llround(plan.settings.crossfadeMs *
                                            original.sampleRate / 1000.0))
        : 0;

    auto joinAllowance = [&](int64_t leftEnd,int64_t rightStart) {
        int64_t allowed=0;
        for (const auto& event:timeEvents) {
            if (event.startSample<=leftEnd && event.endSample>=rightStart)
                allowed=std::max(allowed,std::min(leftEnd-event.startSample,event.endSample-rightStart));
        }
        return allowed;
    };

    // Overlap-add: si accorciano i due lati di N campioni e si inserisce un
    // crossfade di N campioni. La durata finale cala di N per ogni giunzione;
    // il report espone crossfade_frames e removed_samples, quindi la
    // contabilita' non e' silenziosa.
    struct CrossfadeJoin {
        int64_t afterRange = 0;
        int64_t leftStart = 0;
        int64_t leftEnd = 0;
        int64_t rightStart = 0;
        int64_t rightEnd = 0;
        int64_t frames = 0;
    };
    std::vector<CrossfadeJoin> crossfades;
    std::vector<SampleRange> outputRanges = kept;
    std::vector<bool> overlapJoin(kept.size(), false);
    if (requestedCrossfade > 1 && outputRanges.size() > 1) {
        for (size_t i = 1; i < outputRanges.size(); ++i) {
            SampleRange& left = outputRanges[i - 1];
            SampleRange& right = outputRanges[i];
            // Interventi manuali: durata esatta del taglio/50%. Raccordi
            // a rampa, senza sottrarre altri campioni all'intervallo scelto.
            bool manualJoin = false;
            for (const auto& event : timeEvents) if (event.manualOperation) {
                const auto removal = planRemoval(event, original.sampleRate);
                if (removal.valid && removal.removeStart < right.start && removal.removeEnd > left.end)
                    manualJoin = true;
            }
            if (manualJoin) continue;
            const int64_t leftLength = left.end - left.start;
            const int64_t rightLength = right.end - right.start;
            const int64_t frames = std::min({requestedCrossfade,
                                             joinAllowance(left.end,right.start),
                                             std::max<int64_t>(0, leftLength / 2),
                                             std::max<int64_t>(0, rightLength / 2)});
            if (frames <= 1) continue;
            CrossfadeJoin join;
            join.afterRange = static_cast<int64_t>(i - 1);
            join.leftStart = left.end - frames;
            join.leftEnd = left.end;
            join.rightStart = right.start;
            join.rightEnd = right.start + frames;
            join.frames = frames;
            left.end -= frames;
            right.start += frames;
            crossfades.push_back(join);
            overlapJoin[i - 1] = true;
            result->crossfadeFrames += frames;
        }
    }

    size_t joinIndex = 0;
    for (size_t i = 0; i < outputRanges.size(); ++i) {
        const SampleRange& range = outputRanges[i];
        const int64_t length = range.end - range.start;
        const int64_t outStart = outputFrames;
        if (length > 0) {
            result->audio.samples.insert(
                result->audio.samples.end(),
                original.samples.begin() + static_cast<std::ptrdiff_t>(range.start * original.channels),
                original.samples.begin() + static_cast<std::ptrdiff_t>(range.end * original.channels));
            outputFrames += length;
            TimeMapSegment segment;
            segment.originalStart = range.start;
            segment.originalEnd = range.end;
            segment.editedStart = outStart;
            segment.editedEnd = outputFrames;
            result->map.segments.push_back(segment);
        }
        if (joinIndex < crossfades.size() &&
            crossfades[joinIndex].afterRange == static_cast<int64_t>(i)) {
            const CrossfadeJoin& join = crossfades[joinIndex];
            const int64_t outJoinStart = outputFrames;
            for (int64_t f = 0; f < join.frames; ++f) {
                const double t = (static_cast<double>(f) + 0.5) /
                                 static_cast<double>(join.frames);
                const double gainOut = std::cos(t * kPi / 2.0);
                const double gainIn = std::sin(t * kPi / 2.0);
                const int64_t leftFrame = join.leftStart + f;
                const int64_t rightFrame = join.rightStart + f;
                for (int ch = 0; ch < original.channels; ++ch) {
                    const size_t leftIndex = static_cast<size_t>(leftFrame) *
                                             original.channels + ch;
                    const size_t rightIndex = static_cast<size_t>(rightFrame) *
                                              original.channels + ch;
                    result->audio.samples.push_back(static_cast<float>(
                        original.samples[leftIndex] * gainOut +
                        original.samples[rightIndex] * gainIn));
                }
            }
            outputFrames += join.frames;
            TimeMapSegment segment;
            segment.originalStart = join.leftStart;
            segment.originalEnd = join.leftEnd;
            segment.editedStart = outJoinStart;
            segment.editedEnd = outputFrames;
            result->map.segments.push_back(segment);
            ++joinIndex;
        }
        if (i > 0 && !overlapJoin[i - 1] && maxFade > 0) {
            smoothJoin(result->audio, outStart, std::min(maxFade,joinAllowance(kept[i-1].end,kept[i].start)));
        }
    }
    result->map.editedFrames = outputFrames;
    result->removedSamples = original.frameCount() - outputFrames;
    if (outputFrames <= 0) {
        if (error) *error = "il piano rimuove tutto l'audio";
        return false;
    }

    // Una sola curva (minimo dei gain): le sovrapposizioni non moltiplicano
    // l'attenuazione. I click vengono riparati una volta, prima del gain.
    std::vector<float> gainCurve(static_cast<size_t>(outputFrames), 1.f);
    std::vector<RegionOperation> repairs;
    for (const Event& event : approved) {
        if(event.kind==DetectionKind::Click && event.repair && event.repairMethod=="none") {
            EditOperationResult op;op.event=event;op.bypassed=true;
            op.note="Nessuna riparazione: scelta manuale, originale conservato";
            op.clickRepair.requestedMethod="none";op.clickRepair.reason="manual_bypass";
            op.clickRepair.durationMs=1000.*(event.endSample-event.startSample)/original.sampleRate;
            result->operations.push_back(op);continue;
        }
        if (event.mode != EditMode::Attenuate && event.mode != EditMode::Silence) continue;
        bool removedStart = false;
        bool removedEnd = false;
        Event mapped = event;
        mapped.startSample = result->map.mapOriginal(event.startSample, &removedStart);
        mapped.endSample = result->map.mapOriginal(event.endSample, &removedEnd);
        if (mapped.endSample <= mapped.startSample) {
            EditOperationResult operation;
            operation.event = event;
            operation.note = "intervallo rimosso dal taglio";
            result->operations.push_back(operation);
            ++result->rejectedEvents;
            continue;
        }
        const float reduction = event.reductionDb.value_or(plan.settings.reductionDb);
        if (event.mode == EditMode::Attenuate && event.kind == DetectionKind::Click && event.repair &&
            mapped.endSample - mapped.startSample <= original.sampleRate * EditSettings{}.repairMaxMs / 1000.0) {
            RegionOperation operation;
            operation.kind = DetectionKind::Click;
            operation.startSample = mapped.startSample;
            operation.endSample = mapped.endSample;
            operation.reductionDb = reduction;
            operation.repair = true;
            operation.repairMethod = event.repairMethod;
            repairs.push_back(operation);
        } else applyEventGain(result->audio, mapped, reduction, &gainCurve);
        EditOperationResult operation;
        operation.event = event;
        operation.applied = true;
        operation.removedSamples = 0;
        if(event.kind==DetectionKind::Click && !event.repair) {
            operation.clickRepair.requestedMethod="none";operation.clickRepair.reason="gain_operation";
            operation.clickRepair.durationMs=1000.*(event.endSample-event.startSample)/original.sampleRate;
        }
        operation.editedStart = mapped.startSample;
        result->operations.push_back(operation);
        ++result->appliedEvents;
    }

    std::sort(repairs.begin(), repairs.end(), [](const auto& a, const auto& b) { return a.startSample < b.startSample; });
    std::vector<RegionOperation> mergedRepairs;
    for (const auto& repair : repairs) {
        if (!mergedRepairs.empty() && repair.startSample <= mergedRepairs.back().endSample) {
            if(repair.repairMethod!=mergedRepairs.back().repairMethod) {
                if(error)*error="Click sovrapposti con algoritmi diversi: uniforma la scelta";return false;
            }
            mergedRepairs.back().endSample = std::max(mergedRepairs.back().endSample, repair.endSample);
        }
        else mergedRepairs.push_back(repair);
    }
    EditSettings repairSettings;
    repairSettings.prePadMs = repairSettings.postPadMs = 0;
    // Lunghe catene di click non diventano un'interpolazione di parlato.
    for (const auto& repair : mergedRepairs) {
        if (repair.endSample - repair.startSample > original.sampleRate * EditSettings{}.repairMaxMs / 1000.0) {
            if(error)*error="Click sovrapposti oltre 45 ms: rivedere i confini";
            return false;
        }
    }
    const auto repaired=RegionEditor::applyOperations(result->audio.samples, original.channels,
                original.sampleRate, mergedRepairs, repairSettings);
    if(repaired.clickRegionsSkipped) {
        if(error)*error="Contesto insufficiente per la ricostruzione: correggi i confini o scegli Lineare esplicitamente";
        return false;
    }
    for(auto& op:result->operations) {
        if(op.event.kind!=DetectionKind::Click||!op.event.repair||!op.applied)continue;
        const auto end=result->map.mapOriginal(op.event.endSample);
        for(const auto& decision:repaired.clickDecisions) {
            if(op.editedStart<decision.operation.endSample && end>decision.operation.startSample) {
                op.clickRepair=decision.diagnostics;break;
            }
        }
        for(const auto& declined:repaired.declinedClicks) {
            if(op.editedStart<declined.endSample && end>declined.startSample) {
                op.applied=false;op.declined=true;
                op.note="De-click conservato: contesto non sufficiente per una riparazione sicura";
                --result->appliedEvents;++result->declinedEvents;break;
            }
        }
    }
    for (int64_t frame = 0; frame < outputFrames; ++frame)
        for (int ch = 0; ch < original.channels; ++ch)
            result->audio.samples[static_cast<size_t>(frame * original.channels + ch)] *= gainCurve[static_cast<size_t>(frame)];

    for (const Event& event : timeEvents) {
        const PlannedRemoval removal = planRemoval(event, original.sampleRate);
        EditOperationResult operation;
        operation.event = event;
        operation.removedSamples = removal.valid ?
            std::max<int64_t>(0, removal.removeEnd - removal.removeStart) : 0;
        operation.safetyMarginSamples = removal.marginFrames;
        operation.editedStart = result->map.mapOriginal(event.startSample);
        operation.fadeFrames = removal.valid ? std::min(maxFade,
            std::min(removal.removeStart-event.startSample,event.endSample-removal.removeEnd)) : 0;
        for (const auto& join:crossfades) if (join.leftStart>=event.startSample && join.rightEnd<=event.endSample)
            operation.fadeFrames=join.frames;
        operation.applied = removal.valid;
        operation.note = removal.note;
        if (removal.valid && operation.fadeFrames==0 && (maxFade>0 || requestedCrossfade>0))
            operation.note="Taglio esatto; nessun raccordo esterno ai confini approvati. Per un raccordo conserva margini interni.";
        result->operations.push_back(operation);
        if (removal.valid) ++result->appliedEvents;
        else ++result->rejectedEvents;
    }

    applyDocumentEnvelope(result->audio, plan.fade, plan.masterGainDb);

    for (const TimelineMarker& marker : plan.markers) {
        bool removed = false;
        bool removedEnd = false;
        TimelineMarker mapped = marker;
        mapped.timeSample = result->map.mapOriginal(marker.timeSample, &removed);
        if (marker.isRegion) {
            mapped.endSample = result->map.mapOriginal(marker.endSample, &removedEnd);
            if (mapped.endSample < mapped.timeSample) mapped.endSample = mapped.timeSample;
        } else {
            mapped.endSample = mapped.timeSample;
        }
        if (removed || removedEnd) mapped.name += " [dentro taglio]";
        result->mappedMarkers.push_back(mapped);
        TimelineMarker untouched = marker;
        if (untouched.isRegion) untouched.endSample = untouched.endSample;
        result->projectMappedMarkers.push_back(untouched);
    }
    result->voiceMappedMarkers = result->mappedMarkers;
    if (result->markerScope == "project") {
        result->mappedMarkers = result->projectMappedMarkers;
    }

    (void)removedTotal;
    return true;
}

void applyAttenuation(AudioBuffer& audio,const std::vector<Event>& events,const Settings& settings) {
    EditPlan plan;plan.sampleRate=audio.sampleRate;plan.channels=audio.channels;
    plan.settings=settings;plan.events=events;
    EditResult rendered;
    if(applyEditPlan(audio,plan,&rendered,nullptr))audio=std::move(rendered.audio);
}

double rmsDb(const AudioBuffer& audio, int64_t startSample, int64_t endSample,
             int channels) {
    if (audio.empty() || channels <= 0) return -160.0;
    const int64_t frames = audio.frameCount();
    startSample = std::max<int64_t>(0, startSample);
    endSample = std::min<int64_t>(frames, endSample);
    if (endSample <= startSample) return -160.0;

    double sum = 0.0;
    int64_t count = 0;
    for (int64_t frame = startSample; frame < endSample; ++frame) {
        for (int channel = 0; channel < channels; ++channel) {
            const double value = audio.samples[static_cast<size_t>(frame) * channels + channel];
            sum += value * value;
            ++count;
        }
    }
    if (count == 0) return -160.0;
    const double mean = sum / static_cast<double>(count);
    return mean > 1e-20 ? 10.0 * std::log10(mean) : -160.0;
}

bool processFile(const std::string& inputPath,
                 const std::string& outputDirectory,
                 const Settings& settings,
                 Report* report,
                 std::string* error) {
    if (!report) {
        if (error) *error = "report nullo";
        return false;
    }
    const auto startTime = std::chrono::steady_clock::now();

    AudioBuffer original;
    AudioFileInfo info;
    if (!readAudio(inputPath, original, &info, error)) return false;
    if (original.empty()) {
        if (error) *error = "file audio vuoto";
        return false;
    }

    Analysis analysis;
    if (!analyzeFile(inputPath, settings, &analysis, error)) return false;

    const std::filesystem::path inputFs = std::filesystem::u8path(inputPath);
    const std::filesystem::path outDir = std::filesystem::u8path(outputDirectory);
    if (!ensureDirectory(outputDirectory, error)) return false;
    const std::filesystem::path outputFs =
        outDir / (inputFs.stem().string() + "_breathclick.wav");
    std::error_code pathError;
    const std::filesystem::path inputAbs = std::filesystem::absolute(inputFs, pathError);
    const std::filesystem::path outputAbs = std::filesystem::absolute(outputFs, pathError);
    if (!pathError && inputAbs == outputAbs) {
        if (error) *error = "il file di uscita coinciderebbe con l'originale";
        return false;
    }

    for (Event& event : analysis.events) event.approved = !detectionReviewRequired(event.kind,event.classification);
    EditPlan processingPlan;processingPlan.sampleRate=original.sampleRate;processingPlan.channels=original.channels;
    processingPlan.settings=settings;processingPlan.events=analysis.events;
    for(auto& event:processingPlan.events)event.fadeMs=settings.fadeMs;
    EditResult rendered;
    if(!applyEditPlan(original,processingPlan,&rendered,error))return false;
    report->clickDecisions.clear();
    for(const auto& op:rendered.operations)if(op.event.kind==DetectionKind::Click)report->clickDecisions.push_back(op);
    for(const auto& event:analysis.events)if(event.kind==DetectionKind::Click && (!event.approved||!event.enabled)) {
        EditOperationResult op;op.event=event;op.clickRepair.requestedMethod=event.repairMethod;
        op.clickRepair.reason="review_required";op.note="Proposta in attesa di revisione";report->clickDecisions.push_back(op);
    }
    AudioBuffer processed=std::move(rendered.audio);

    const WavFormat format = chooseFormat(info);
    if (!writeWavWithMetadataAtomic(inputPath, outputFs.u8string(), processed, format, error,
                                    &report->wav)) return false;

    report->analysis = analysis;
    report->outputPath = outputFs.u8string();
    report->settings = settings;
    report->metrics.clear();
    report->processedEvents = 0;
    report->skippedEvents = 0;

    std::string outputHashError;
    if (!sha256File(report->outputPath, report->outputSha256, &outputHashError)) {
        report->outputSha256 = "non-calcolato: " + outputHashError;
    }

    // Concentra la misura nella parte centrale del respiro, lontana dalle
    // rampe: li' l'attenuazione deve essere quella nominale.
    for (const Event& event : analysis.events) {
        const bool declined=std::any_of(rendered.operations.begin(),rendered.operations.end(),[&](const auto& op){return op.declined&&op.event.startSample==event.startSample&&op.event.endSample==event.endSample;});
        if (!event.enabled || !event.approved || declined) {
            ++report->skippedEvents;
            continue;
        }
        const int64_t length = event.endSample - event.startSample;
        if (length <= 0) continue;
        const int64_t margin = std::max<int64_t>(1, length / 4);
        const int64_t start = length>2?event.startSample+margin:event.startSample;
        const int64_t end = length>2?event.endSample-margin:event.endSample;
        if (end <= start) continue;

        EventMetric metric;
        metric.startSample = event.startSample;
        metric.endSample = event.endSample;
        metric.rmsBeforeDb = rmsDb(original, start, end, original.channels);
        metric.rmsAfterDb = rmsDb(processed, start, end, original.channels);
        metric.deltaDb = metric.rmsAfterDb - metric.rmsBeforeDb;
        report->metrics.push_back(metric);
        ++report->processedEvents;
    }

    // Fuori dagli intervalli trattati il segnale deve restare invariato.
    double sumBefore = 0.0;
    double sumAfter = 0.0;
    int64_t outsideFrames = 0;
    std::vector<char> inside(static_cast<size_t>(original.frameCount()), 0);
    const int64_t fadeFrames = std::max<int64_t>(
        1, static_cast<int64_t>(std::llround(settings.fadeMs * original.sampleRate / 1000.0)));
    for (const Event& event : analysis.events) {
        if (!event.enabled || !event.approved) continue;
        const int64_t from = std::max<int64_t>(0, event.startSample - fadeFrames);
        const int64_t to = std::min<int64_t>(original.frameCount(), event.endSample + fadeFrames);
        for (int64_t frame = from; frame < to; ++frame) inside[static_cast<size_t>(frame)] = 1;
    }
    double maxDelta = 0.0;
    double sumDelta = 0.0;
    int64_t windows = 0;
    for (int64_t frame = 0; frame < original.frameCount(); ++frame) {
        if (inside[static_cast<size_t>(frame)]) continue;
        for (int channel = 0; channel < original.channels; ++channel) {
            const size_t index = static_cast<size_t>(frame) * original.channels + channel;
            const double before = original.samples[index];
            const double after = processed.samples[index];
            sumBefore += before * before;
            sumAfter += after * after;
            ++outsideFrames;
        }
        if (frame % 480 == 0) {
            const int64_t windowEnd = std::min(frame + 480, original.frameCount());
            bool windowInside = false;
            for (int64_t check = frame; check < windowEnd; ++check) {
                if (inside[static_cast<size_t>(check)]) {
                    windowInside = true;
                    break;
                }
            }
            if (!windowInside) {
                const double b = rmsDb(original, frame, windowEnd, original.channels);
                const double a = rmsDb(processed, frame, windowEnd, original.channels);
                if (b > -120.0) {
                    const double delta = std::abs(a - b);
                    maxDelta = std::max(maxDelta, delta);
                    sumDelta += delta;
                    ++windows;
                }
            }
        }
    }
    (void)outsideFrames;
    report->outsideMaxDeltaDb = maxDelta;
    report->outsideMeanDeltaDb = windows > 0 ? sumDelta / static_cast<double>(windows) : 0.0;

    const auto endTime = std::chrono::steady_clock::now();
    report->processingSeconds =
        std::chrono::duration<double>(endTime - startTime).count();
    report->error.clear();
    return true;
}

bool processFolder(const std::string& inputDirectory,
                   const std::string& outputDirectory,
                   const Settings& settings,
                   const std::string& batchReportPath,
                   std::vector<Report>* reports,
                   std::string* error) {
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::u8path(inputDirectory);
    if (!std::filesystem::is_directory(root, ec)) {
        if (error) *error = "cartella di ingresso non trovata";
        return false;
    }
    if (!ensureDirectory(outputDirectory, error)) return false;

    std::vector<std::filesystem::path> files;
    const std::filesystem::path outputAbs =
        std::filesystem::absolute(std::filesystem::u8path(outputDirectory), ec).lexically_normal();
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const auto candidate = std::filesystem::absolute(it->path(), ec).lexically_normal();
        if (!ec && it->is_directory(ec)) {
            const std::string dirName = it->path().filename().u8string();
            if (candidate == outputAbs ||
                dirName.rfind("breathclick_out", 0) == 0) {
                it.disable_recursion_pending();
                continue;
            }
        }
        if (!it->is_regular_file(ec)) continue;
        if (isSupportedAudioPath(it->path().u8string())) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());

    std::vector<Report> local;
    std::ostringstream errors;
    for (const auto& file : files) {
        Report report;
        std::string fileError;
        if (!processFile(file.u8string(), outputDirectory, settings, &report, &fileError)) {
            errors << file.u8string() << ": " << fileError << "\n";
            continue;
        }
        const std::filesystem::path reportBase =
            std::filesystem::u8path(outputDirectory) / (file.stem().string() + ".report.json");
        std::string reportError;
        writeReportJson(reportBase.u8string(), report, &reportError);
        local.push_back(std::move(report));
    }

    if (reports) *reports = local;

    if (!batchReportPath.empty()) {
        std::ofstream out(nativeFilePath(std::filesystem::u8path(batchReportPath)), std::ios::binary);
        if (out) {
            out << "{\n  \"schema\": 1,\n  \"input_directory\": \""
                << jsonEscape(inputDirectory) << "\",\n"
                << "  \"output_directory\": \"" << jsonEscape(outputDirectory) << "\",\n"
                << "  \"files\": " << local.size() << ",\n"
                << "  \"errors\": \"" << jsonEscape(errors.str()) << "\",\n"
                << "  \"reports\": [\n";
            for (size_t i = 0; i < local.size(); ++i) {
                out << "    \"" << jsonEscape(local[i].outputPath) << "\"";
                if (i + 1 < local.size()) out << ",";
                out << "\n";
            }
            out << "  ]\n}\n";
        }
    }

    if (local.empty()) {
        if (error) *error = errors.str().empty() ? "nessun WAV trovato" : errors.str();
        return false;
    }
    return true;
}

namespace {

const char* findKeyIn(const std::string& object, const std::string& key) {
    // JSON object order is unspecified. Never mistake an event's `source`
    // ("ai"/"dsp") for the root audio filename, or a quoted word for a key.
    int depth = 0;
    for (size_t p = 0; p < object.size();) {
        const char c = object[p];
        if (c == '"') {
            std::string value;
            if (!decodeJsonString(object, p, value)) return nullptr;
            size_t next = p;
            while (next < object.size() && std::isspace(static_cast<unsigned char>(object[next]))) ++next;
            if (depth == 1 && value == key && next < object.size() && object[next] == ':')
                return object.c_str() + next;
        } else {
            if (c == '{' || c == '[') ++depth;
            if (c == '}' || c == ']') --depth;
            ++p;
        }
    }
    return nullptr;
}

const char* skipJsonSpace(const char* p) {
    while (p && *p && std::isspace(static_cast<unsigned char>(*p))) ++p;
    return p;
}

bool readObjectString(const std::string& object, const std::string& key,
                      std::string& out) {
    const char* p = findKeyIn(object, key);
    if (!p) return false;
    p = skipJsonSpace(p + 1);
    size_t position = static_cast<size_t>(p - object.c_str());
    return decodeJsonString(object, position, out);
}

bool readObjectNumber(const std::string& object, const std::string& key,
                      double& out) {
    const char* p = findKeyIn(object, key);
    if (!p) return false;
    p = skipJsonSpace(p + 1);
    char* end = nullptr;
    out = std::strtod(p, &end);
    return end != p;
}

bool readObjectBool(const std::string& object, const std::string& key, bool& out) {
    const char* p = findKeyIn(object, key);
    if (!p) return false;
    p = skipJsonSpace(p + 1);
    if (std::strncmp(p, "true", 4) == 0) { out = true; return true; }
    if (std::strncmp(p, "false", 5) == 0) { out = false; return true; }
    return false;
}

std::vector<std::string> objectArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    const char* member = findKeyIn(json, key);
    if (!member) return result;
    const char* array = skipJsonSpace(member + 1);
    if (*array != '[') return result;
    const size_t bracket = static_cast<size_t>(array-json.c_str());
    std::string current;
    int depth = 0;
    bool inString = false;
    for (size_t i = bracket + 1; i < json.size(); ++i) {
        const char c = json[i];
        if (inString) {
            current += c;
            if (c == '\\' && i + 1 < json.size()) current += json[++i];
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') {
            inString = true;
            current += c;
        } else if (c == '{') {
            if (depth == 0) current.clear();
            ++depth;
            current += c;
        } else if (c == '}') {
            --depth;
            current += c;
            if (depth == 0) result.push_back(current);
        } else if (depth > 0) {
            current += c;
        } else if (c == ']') {
            break;
        }
    }
    return result;
}

} // namespace

namespace {

uint16_t readLe16(const unsigned char* data) {
    return static_cast<uint16_t>(data[0] | (static_cast<uint16_t>(data[1]) << 8));
}

uint32_t readLe32(const unsigned char* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void writeLe16(std::ofstream& out, uint16_t value) {
    const unsigned char bytes[2] = {
        static_cast<unsigned char>(value & 0xFF),
        static_cast<unsigned char>((value >> 8) & 0xFF)};
    out.write(reinterpret_cast<const char*>(bytes), 2);
}

void writeLe32(std::ofstream& out, uint32_t value) {
    const unsigned char bytes[4] = {
        static_cast<unsigned char>(value & 0xFF),
        static_cast<unsigned char>((value >> 8) & 0xFF),
        static_cast<unsigned char>((value >> 16) & 0xFF),
        static_cast<unsigned char>((value >> 24) & 0xFF)};
    out.write(reinterpret_cast<const char*>(bytes), 4);
}

void encodeStudioSample(unsigned char* out, float value, int bits, bool isFloat) {
    value = std::max(-1.0f, std::min(1.0f, value));
    if (isFloat) {
        std::memcpy(out, &value, sizeof(float));
    } else if (bits == 16) {
        const int32_t scaled = static_cast<int32_t>(std::lround(value * 32767.0f));
        const uint16_t raw = static_cast<uint16_t>(std::max(-32768, std::min(32767, scaled)));
        out[0] = static_cast<unsigned char>(raw & 0xFF);
        out[1] = static_cast<unsigned char>((raw >> 8) & 0xFF);
    } else {
        const int32_t scaled = static_cast<int32_t>(std::lround(value * 8388607.0f));
        const int32_t clamped = std::max(-8388608, std::min(8388607, scaled));
        out[0] = static_cast<unsigned char>(clamped & 0xFF);
        out[1] = static_cast<unsigned char>((clamped >> 8) & 0xFF);
        out[2] = static_cast<unsigned char>((clamped >> 16) & 0xFF);
    }
}

} // namespace

struct RiffChunk {
    char id[4] = {};
    uint32_t size = 0;
    std::vector<unsigned char> data;
};

bool timingSensitiveChunk(const char* id, std::string* reason) {
    const std::string name(id, 4);
    if (name == "cue ") {
        if (reason) *reason = "cue point con offset campione non piu' validi";
        return true;
    }
    if (name == "smpl") {
        if (reason) *reason = "loop point con offset campione non piu' validi";
        return true;
    }
    if (name == "inst") {
        if (reason) *reason = "strumento con riferimenti a campioni";
        return true;
    }
    if (name == "PEAK") {
        if (reason) *reason = "peak chunk con offset campione non piu' validi";
        return true;
    }
    return false;
}

void writeBytes(std::ofstream& out, const void* data, size_t bytes) {
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
}

void writeChunkHeader(std::ofstream& out, const char* id, uint32_t size) {
    out.write(id, 4);
    writeLe32(out, size);
}

void padChunk(std::ofstream& out, uint32_t size, bool* padded) {
    if ((size & 1u) == 0) return;
    const unsigned char zero = 0;
    writeBytes(out, &zero, 1);
    if (padded) *padded = true;
}

bool writeWavWithMetadata(const std::string& sourcePath,
                          const std::string& outputPath,
                          const AudioBuffer& audio,
                          WavFormat format,
                          std::string* error,
                          WavWriteReport* report) {
    if (sameFilePath(sourcePath, outputPath)) {
        if (error) *error = "l'esportazione non puo' sovrascrivere l'audio originale";
        return false;
    }

    const int bits = format == WavFormat::Pcm16 ? 16 : (format == WavFormat::Pcm24 ? 24 : 32);
    const bool isFloat = format == WavFormat::Float32;
    const int channels = std::max(1, audio.channels);
    const int sampleRate = std::max(1, audio.sampleRate);
    const int64_t frames = audio.frameCount();
    const int bytesPerSample = bits / 8;
    const uint64_t dataBytes = static_cast<uint64_t>(frames) *
                               static_cast<uint64_t>(channels * bytesPerSample);
    if (dataBytes > 0xFFFFFFFFull) {
        if (error) *error = "WAV troppo grande per RIFF";
        return false;
    }

    std::vector<RiffChunk> sourceChunks;
    bool sourceHasFmt = false;
    bool sourceHasData = false;
    std::ifstream source(nativeFilePath(std::filesystem::u8path(sourcePath)), std::ios::binary);
    if (source) {
        unsigned char header[12] = {};
        source.read(reinterpret_cast<char*>(header), 12);
        if (source && std::memcmp(header, "RIFF", 4) == 0 &&
            std::memcmp(header + 8, "WAVE", 4) == 0) {
            while (source) {
                unsigned char chunkHeader[8] = {};
                source.read(reinterpret_cast<char*>(chunkHeader), 8);
                if (source.gcount() != 8) break;
                const uint32_t size = readLe32(chunkHeader + 4);
                if (size > 0x7FFFFFFFu) break;
                RiffChunk chunk;
                std::memcpy(chunk.id, chunkHeader, 4);
                chunk.size = size;
                chunk.data.resize(size);
                if (size > 0) {
                    source.read(reinterpret_cast<char*>(chunk.data.data()), size);
                    if (!source) break;
                }
                if (size & 1u) {
                    char pad = 0;
                    source.read(&pad, 1);
                    if (!source) break;
                }
                if (std::memcmp(chunk.id, "fmt ", 4) == 0) sourceHasFmt = true;
                if (std::memcmp(chunk.id, "data", 4) == 0) sourceHasData = true;
                sourceChunks.push_back(std::move(chunk));
            }
        }
    }

    std::ofstream out(nativeFilePath(std::filesystem::u8path(outputPath)), std::ios::binary);
    if (!out) {
        if (error) *error = "impossibile scrivere il WAV di uscita";
        return false;
    }

    if (report) {
        report->sourceChunks.clear();
        report->writtenChunks.clear();
        report->dataPadding = false;
        report->riffSizePatched = false;
        for (const RiffChunk& chunk : sourceChunks) {
            WavChunkInfo info;
            info.id = std::string(chunk.id, 4);
            info.size = chunk.size;
            report->sourceChunks.push_back(info);
        }
    }

    out.write("RIFF", 4);
    writeLe32(out, 0);
    out.write("WAVE", 4);

    const uint32_t fmtSize = 16;
    auto writeFmt = [&]() {
        writeChunkHeader(out, "fmt ", fmtSize);
        writeLe16(out, isFloat ? 3 : 1);
        writeLe16(out, static_cast<uint16_t>(channels));
        writeLe32(out, static_cast<uint32_t>(sampleRate));
        writeLe32(out, static_cast<uint32_t>(sampleRate * channels * bytesPerSample));
        writeLe16(out, static_cast<uint16_t>(channels * bytesPerSample));
        writeLe16(out, static_cast<uint16_t>(bits));
        if (report) {
            WavChunkInfo info;
            info.id = "fmt ";
            info.size = fmtSize;
            info.copied = true;
            info.note = "rigenerato dal formato di uscita";
            report->writtenChunks.push_back(info);
        }
    };

    auto writeData = [&]() {
        writeChunkHeader(out, "data", static_cast<uint32_t>(dataBytes));
        std::vector<unsigned char> frame(static_cast<size_t>(channels * bytesPerSample));
        for (int64_t i = 0; i < frames; ++i) {
            for (int channel = 0; channel < channels; ++channel) {
                encodeStudioSample(frame.data() + channel * bytesPerSample,
                                   audio.samples[static_cast<size_t>(i) * audio.channels + channel],
                                   bits, isFloat);
            }
            writeBytes(out, frame.data(), frame.size());
        }
        padChunk(out, static_cast<uint32_t>(dataBytes),
                 report ? &report->dataPadding : nullptr);
        if (report) {
            WavChunkInfo info;
            info.id = "data";
            info.size = dataBytes;
            info.copied = true;
            info.note = (dataBytes & 1u) ? "riscritto, padding RIFF aggiunto"
                                         : "riscritto";
            report->writtenChunks.push_back(info);
        }
    };

    if (!sourceHasFmt) writeFmt();
    bool fmtSeen = false;
    bool dataSeen = false;
    for (const RiffChunk& chunk : sourceChunks) {
        const std::string id(chunk.id, 4);
        if (id == "fmt ") {
            writeFmt();
            fmtSeen = true;
        } else if (id == "data") {
            if (!dataSeen) {
                writeData();
                dataSeen = true;
            }
            if (report) {
                WavChunkInfo info;
                info.id = "data";
                info.size = chunk.size;
                info.note = "data originale sostituito dai campioni editati";
                report->writtenChunks.push_back(info);
            }
        } else {
            std::string reason;
            WavInfo sourceInfo;
            const bool sameTimeline = probeWav(sourcePath, sourceInfo, nullptr) &&
                sourceInfo.frames == frames && sourceInfo.sampleRate == sampleRate;
            if (!sameTimeline && timingSensitiveChunk(chunk.id, &reason)) {
                if (report) {
                    WavChunkInfo info;
                    info.id = id;
                    info.size = chunk.size;
                    info.note = "scartato: " + reason;
                    report->writtenChunks.push_back(info);
                }
                continue;
            }
            writeChunkHeader(out, chunk.id, chunk.size);
            if (!chunk.data.empty()) writeBytes(out, chunk.data.data(), chunk.data.size());
            padChunk(out, chunk.size, nullptr);
            if (report) {
                WavChunkInfo info;
                info.id = id;
                info.size = chunk.size;
                info.copied = true;
                info.note = id == "bext"
                    ? "preservato byte per byte; TimeReference non rimappato"
                    : "preservato";
                report->writtenChunks.push_back(info);
            }
        }
    }
    if (!fmtSeen && sourceHasFmt) writeFmt();
    if (!dataSeen) writeData();

    const std::streampos endPosition = out.tellp();
    const uint64_t riffSize = static_cast<uint64_t>(endPosition) - 8ull;
    if (riffSize > 0xFFFFFFFFull) {
        out.close();
        if (error) *error = "WAV di uscita troppo grande per RIFF";
        return false;
    }
    out.seekp(4, std::ios::beg);
    writeLe32(out, static_cast<uint32_t>(riffSize));
    out.close();
    if (!out.good() && !out.eof()) {
        if (error) *error = "errore di scrittura del WAV";
        return false;
    }
    if (report) report->riffSizePatched = true;
    return true;
}

bool writeWavWithMetadataAtomic(const std::string& sourcePath,
                                const std::string& outputPath,
                                const AudioBuffer& audio,
                                WavFormat format,
                                std::string* error,
                                WavWriteReport* report) {
    if (sameFilePath(sourcePath, outputPath)) {
        if (error) *error = "l'esportazione non puo' sovrascrivere l'audio originale";
        return false;
    }

    const std::filesystem::path target = nativeFilePath(std::filesystem::u8path(outputPath));
    std::filesystem::path temp = target;
    temp += ".part";
    std::error_code ec;
    if (!writeWavWithMetadata(sourcePath, temp.u8string(), audio, format, error, report)) {
        std::filesystem::remove(temp, ec);
        return false;
    }
#ifdef _WIN32
    if (!MoveFileExW(temp.wstring().c_str(), target.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temp, ec);
        if (error) *error = "impossibile sostituire il WAV di uscita";
        return false;
    }
#else
    std::filesystem::rename(temp, target, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        if (error) *error = "impossibile sostituire il WAV di uscita";
        return false;
    }
#endif
    return true;
}

bool writeEditPlanJson(const std::string& path, const EditPlan& plan,
                       std::string* error) {
    if (sameFilePath(path, plan.sourcePath)) {
        if (error) *error = "il report o piano non puo' sovrascrivere l'audio originale";
        return false;
    }
    std::ostringstream out;
    out << std::setprecision(17);
    std::string sourceRelative;
    std::string sourceSha;
    std::error_code relativeError;
    const std::filesystem::path sourceFs = std::filesystem::u8path(plan.sourcePath);
    const std::filesystem::path planFs = std::filesystem::u8path(path);
    if (std::filesystem::exists(nativeFilePath(sourceFs), relativeError)) {
        sourceRelative = sourceFs.lexically_relative(planFs.parent_path()).u8string();
        if (sourceRelative == "." ) sourceRelative.clear();
        std::string hashError;
        sha256File(plan.sourcePath, sourceSha, &hashError);
        if (!hashError.empty()) sourceSha.clear();
    }
    out << "{\n  \"schema\": 2,\n"
        << "  \"source\": \"" << jsonEscape(plan.sourcePath) << "\",\n"
        << "  \"source_relative\": \"" << jsonEscape(sourceRelative) << "\",\n"
        << "  \"source_sha256\": \"" << jsonEscape(sourceSha) << "\",\n"
        << "  \"preserve_duration\": " << (plan.preserveDuration ? "true" : "false") << ",\n"
        << "  \"master_gain_db\": " << plan.masterGainDb << ",\n"
        << "  \"fade_in_seconds\": " << plan.fade.inSeconds << ",\n"
        << "  \"fade_out_seconds\": " << plan.fade.outSeconds << ",\n"
        << "  \"fade_in_shape\": " << static_cast<int>(plan.fade.inShape) << ",\n"
        << "  \"fade_out_shape\": " << static_cast<int>(plan.fade.outShape) << ",\n"
        << "  \"fade_in_enabled\": " << (plan.fade.inEnabled ? "true" : "false") << ",\n"
        << "  \"fade_out_enabled\": " << (plan.fade.outEnabled ? "true" : "false") << ",\n"
        << "  \"sample_rate\": " << plan.sampleRate << ",\n"
        << "  \"channels\": " << plan.channels << ",\n"
        << "  \"reduction_db\": " << plan.settings.reductionDb << ",\n"
        << "  \"fade_ms\": " << plan.settings.fadeMs << ",\n"
        << "  \"crossfade_ms\": " << plan.settings.crossfadeMs << ",\n"
        << "  \"detect_breath\": " << (plan.settings.detectBreath?"true":"false") << ",\n"
        << "  \"detect_clicks\": " << (plan.settings.detectClicks?"true":"false") << ",\n"
        << "  \"click_sensitivity\": " << plan.settings.clickSensitivity << ",\n"
        << "  \"marker_scope\": \"" << jsonEscape(plan.markerScope) << "\",\n"
        << "  \"detector\": \"" << jsonEscape(plan.settings.detector) << "\",\n"
        << "  \"effective_detector\": \"" << jsonEscape(plan.analysis.detector.empty() ? "unknown_legacy" : plan.analysis.detector) << "\",\n"
        << "  \"ai_inference_frames\": " << plan.analysis.aiFrames << ",\n"
        << "  \"events\": [\n";
    for (size_t i = 0; i < plan.events.size(); ++i) {
        const Event& event = plan.events[i];
        out << "    {\"kind\": " << static_cast<int>(event.kind);
        if (event.reductionDb) out << ", \"event_reduction_db\": " << *event.reductionDb;
        out << ", \"repair\": " << (event.repair ? "true" : "false")
            << ", \"start\": " << event.startSample
            << ", \"end\": " << event.endSample
            << ", \"score\": " << event.score
            << ", \"enabled\": " << (event.enabled ? "true" : "false")
            << ", \"approved\": " << (event.approved ? "true" : "false")
            << ", \"state\": \"" << jsonEscape(event.state) << "\""
            << ", \"classification\": \"" << jsonEscape(event.classification.empty() ? event.source : event.classification) << "\""
            << ", \"decision_source\": \"" << jsonEscape(event.decisionSource) << "\""
            << ", \"training_eligible\": " << (event.trainingEligible ? "true" : "false")
            << ", \"repair_method\": \"" << jsonEscape(event.repairMethod) << "\""
            << ", \"repair_feedback\": \"" << jsonEscape(event.repairFeedback) << "\""
            << ", \"repair_feedback_method\": \"" << jsonEscape(event.repairFeedbackMethod) << "\""
            << ", \"learning_label\": \"" << jsonEscape(event.learningLabel) << "\""
            << ", \"manual_operation\": " << (event.manualOperation ? "true" : "false")
            << ", \"mode\": \"" << editModeName(event.mode) << "\""
            << ", \"keep_fraction\": " << event.keepFraction
            << ", \"fade_ms\": " << event.fadeMs
            << ", \"safety_margin_ms\": " << event.safetyMarginMs
            << ", \"source\": \"" << jsonEscape(event.source) << "\""
            << ", \"reason\": \"" << jsonEscape(event.reason) << "\""
            << ", \"dsp_score\": " << event.dspScore
            << ", \"ai_score\": " << event.aiScore
            << ", \"ai_max_probability\": " << event.aiMaxProbability << "}";
        if (i + 1 < plan.events.size()) out << ",";
        out << "\n";
    }
    out << "  ],\n  \"markers\": [\n";
    for (size_t i = 0; i < plan.markers.size(); ++i) {
        const TimelineMarker& marker = plan.markers[i];
        out << "    {\"time\": " << marker.timeSample
            << ", \"end\": " << marker.endSample
            << ", \"is_region\": " << (marker.isRegion ? "true" : "false")
            << ", \"name\": \"" << jsonEscape(marker.name) << "\"}";
        if (i + 1 < plan.markers.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n}\n";
    if (!validJsonDocument(out.str())) {
        if (error) *error = "dati del report o piano non validi";
        return false;
    }
    return writeTextAtomic(path, out.str(), error);
}

bool writeEditReportJson(const std::string& path, const EditPlan& plan,
                         const EditResult& result, const std::string& outputPath,
                         std::string* error) {
    if (sameFilePath(path, plan.sourcePath)) {
        if (error) *error = "il report o piano non puo' sovrascrivere l'audio originale";
        return false;
    }
    std::ostringstream out;
    out << std::setprecision(17);
    const double sr = std::max(1, plan.sampleRate);
    std::string sourceRelative;
    std::string sourceSha;
    std::error_code relativeError;
    const std::filesystem::path sourceFs = std::filesystem::u8path(plan.sourcePath);
    const std::filesystem::path planFs = std::filesystem::u8path(path);
    if (std::filesystem::exists(nativeFilePath(sourceFs), relativeError)) {
        sourceRelative = sourceFs.lexically_relative(planFs.parent_path()).u8string();
        if (sourceRelative == "." ) sourceRelative.clear();
        std::string hashError;
        sha256File(plan.sourcePath, sourceSha, &hashError);
        if (!hashError.empty()) sourceSha.clear();
    }
    out << "{\n  \"schema\": 2,\n"
        << "  \"source\": \"" << jsonEscape(plan.sourcePath) << "\",\n"
        << "  \"source_relative\": \"" << jsonEscape(sourceRelative) << "\",\n"
        << "  \"source_sha256\": \"" << jsonEscape(sourceSha) << "\",\n"
        << "  \"output\": \"" << jsonEscape(outputPath) << "\",\n"
        << "  \"preserve_duration\": " << (plan.preserveDuration ? "true" : "false") << ",\n"
        << "  \"sample_rate\": " << plan.sampleRate << ",\n"
        << "  \"channels\": " << plan.channels << ",\n"
        << "  \"original_frames\": " << result.map.originalFrames << ",\n"
        << "  \"edited_frames\": " << result.map.editedFrames << ",\n"
        << "  \"removed_samples\": " << result.removedSamples << ",\n"
        << "  \"removed_seconds\": " << std::setprecision(9)
        << (static_cast<double>(result.removedSamples) / sr) << ",\n"
        << "  \"applied_events\": " << result.appliedEvents << ",\n"
        << "  \"rejected_events\": " << result.rejectedEvents << ",\n"
        << "  \"declined_events\": " << result.declinedEvents << ",\n"
        << "  \"operations\": [\n";
    for (size_t i = 0; i < result.operations.size(); ++i) {
        const EditOperationResult& op = result.operations[i];
        out << "    {\"start\": " << op.event.startSample
            << ", \"kind\": " << static_cast<int>(op.event.kind)
            << ", \"end\": " << op.event.endSample
            << ", \"mode\": \"" << editModeName(op.event.mode) << "\""
            << ", \"applied\": " << (op.applied ? "true" : "false")
            << ", \"declined\": " << (op.declined ? "true" : "false")
            << ", \"bypassed\": " << (op.bypassed ? "true" : "false")
            << ", \"outcome\": \"" << operationOutcome(op) << "\""
            << ", \"click_strategy\": ";
        writeClickDiagnostics(out,op.clickRepair);
        out
            << ", \"edited_start\": " << op.editedStart
            << ", \"removed_samples\": " << op.removedSamples
            << ", \"safety_margin_samples\": " << op.safetyMarginSamples
            << ", \"fade_frames\": " << op.fadeFrames
            << ", \"note\": \"" << jsonEscape(op.note) << "\"}";
        if (i + 1 < result.operations.size()) out << ",";
        out << "\n";
    }
    out << "  ],\n  \"event_outcomes\": [\n";
    for (size_t i = 0; i < plan.events.size(); ++i) {
        const auto& event = plan.events[i];
        out << "    {\"event_index\": " << i << ", \"kind\": " << static_cast<int>(event.kind)
            << ", \"start\": " << event.startSample << ", \"end\": " << event.endSample
            << ", \"outcome\": \"" << eventOutcome(event, &result) << "\"}"
            << (i + 1 < plan.events.size() ? "," : "") << "\n";
    }
    out << "  ],\n  \"time_map\": [\n";
    for (size_t i = 0; i < result.map.segments.size(); ++i) {
        const TimeMapSegment& seg = result.map.segments[i];
        out << "    {\"original_start\": " << seg.originalStart
            << ", \"original_end\": " << seg.originalEnd
            << ", \"edited_start\": " << seg.editedStart
            << ", \"edited_end\": " << seg.editedEnd << "}";
        if (i + 1 < result.map.segments.size()) out << ",";
        out << "\n";
    }
    out << "  ],\n  \"wav_chunks\": [\n";
    for (size_t i = 0; i < result.wav.writtenChunks.size(); ++i) {
        const WavChunkInfo& chunk = result.wav.writtenChunks[i];
        out << "    {\"id\": \"" << jsonEscape(chunk.id) << "\""
            << ", \"size\": " << chunk.size
            << ", \"copied\": " << (chunk.copied ? "true" : "false")
            << ", \"note\": \"" << jsonEscape(chunk.note) << "\"}";
        if (i + 1 < result.wav.writtenChunks.size()) out << ",";
        out << "\n";
    }
    out << "  ],\n  \"wav_padding\": " << (result.wav.dataPadding ? "true" : "false")
        << ",\n  \"riff_size_patched\": " << (result.wav.riffSizePatched ? "true" : "false")
        << ",\n  \"crossfade_frames\": " << result.crossfadeFrames
        << ",\n  \"marker_scope\": \"" << jsonEscape(result.markerScope) << "\",\n";
    auto writeMarkers = [&](const char* key, const std::vector<TimelineMarker>& markers,
                            bool last) {
        out << "  \"" << key << "\": [\n";
        for (size_t i = 0; i < markers.size(); ++i) {
            const TimelineMarker& marker = markers[i];
            out << "    {\"time\": " << marker.timeSample
                << ", \"end\": " << marker.endSample
                << ", \"is_region\": " << (marker.isRegion ? "true" : "false")
                << ", \"name\": \"" << jsonEscape(marker.name) << "\"}";
            if (i + 1 < markers.size()) out << ",";
            out << "\n";
        }
        out << "  ]" << (last ? "\n" : ",\n");
    };
    writeMarkers("markers", result.mappedMarkers, false);
    writeMarkers("voice_markers", result.voiceMappedMarkers, false);
    writeMarkers("project_markers", result.projectMappedMarkers, true);
    out << "}\n";
    if (!validJsonDocument(out.str())) {
        if (error) *error = "dati del report o piano non validi";
        return false;
    }
    return writeTextAtomic(path, out.str(), error);
}

bool loadEditPlanJson(const std::string& path, EditPlan* output,
                      std::string* error) {
    if (!output) return false;
    EditPlan pending;
    auto* plan = &pending;
    std::ifstream in(nativeFilePath(std::filesystem::u8path(path)), std::ios::binary);
    if (!in) {
        if (error) *error = "piano non apribile";
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string json = buffer.str();
    if (!validJsonDocument(json)) { if (error) *error = "piano JSON incompleto o non valido"; return false; }
    plan->schema = 2;
    if (!readObjectString(json, "source", plan->sourcePath)) {
        if (error) *error = "piano senza campo source";
        return false;
    }
    readObjectString(json, "source_relative", plan->sourceRelative);
    readObjectString(json, "source_sha256", plan->sourceSha256);
    readObjectBool(json, "preserve_duration", plan->preserveDuration);
    double number = 0.0;
    if (readObjectNumber(json, "sample_rate", number)) plan->sampleRate = static_cast<int>(number);
    if (readObjectNumber(json, "channels", number)) plan->channels = static_cast<int>(number);
    if (readObjectNumber(json, "reduction_db", number)) plan->settings.reductionDb = static_cast<float>(number);
    if (readObjectNumber(json, "fade_ms", number)) plan->settings.fadeMs = number;
    if (readObjectNumber(json, "crossfade_ms", number)) plan->settings.crossfadeMs = number;
    readObjectString(json, "marker_scope", plan->markerScope);
    readObjectString(json, "detector", plan->settings.detector);
    readObjectBool(json, "detect_breath", plan->settings.detectBreath);
    readObjectBool(json, "detect_clicks", plan->settings.detectClicks);
    if(readObjectNumber(json,"click_sensitivity",number))plan->settings.clickSensitivity=static_cast<float>(number);
    readObjectString(json, "effective_detector", plan->analysis.detector);
    if (plan->analysis.detector == "unknown_legacy") plan->analysis.detector.clear();
    if (readObjectNumber(json, "ai_inference_frames", number)) plan->analysis.aiFrames = static_cast<int64_t>(number);

    if (readObjectNumber(json, "master_gain_db", number)) plan->masterGainDb = static_cast<float>(number);
    if (readObjectNumber(json, "fade_in_seconds", number)) plan->fade.inSeconds = number;
    if (readObjectNumber(json, "fade_out_seconds", number)) plan->fade.outSeconds = number;
    if (readObjectNumber(json, "fade_in_shape", number)) plan->fade.inShape = static_cast<FadeShape>(static_cast<int>(number));
    if (readObjectNumber(json, "fade_out_shape", number)) plan->fade.outShape = static_cast<FadeShape>(static_cast<int>(number));
    readObjectBool(json, "fade_in_enabled", plan->fade.inEnabled);
    readObjectBool(json, "fade_out_enabled", plan->fade.outEnabled);

    plan->events.clear();
    for (const std::string& object : objectArray(json, "events")) {
        Event event;
        if (readObjectNumber(object, "kind", number)) event.kind = number == 1 ? DetectionKind::Click : DetectionKind::Breath;
        if (readObjectNumber(object, "event_reduction_db", number)) event.reductionDb = static_cast<float>(number);
        readObjectBool(object, "repair", event.repair);
        if (readObjectNumber(object, "start", number)) event.startSample = static_cast<int64_t>(number);
        if (readObjectNumber(object, "end", number)) event.endSample = static_cast<int64_t>(number);
        if (readObjectNumber(object, "score", number)) event.score = static_cast<float>(number);
        readObjectBool(object, "enabled", event.enabled);
        readObjectBool(object, "approved", event.approved);
        readObjectString(object, "state", event.state);
        readObjectString(object, "classification", event.classification);
        readObjectString(object, "learning_label", event.learningLabel);
        readObjectString(object, "repair_method", event.repairMethod);
        readObjectString(object, "repair_feedback", event.repairFeedback);
        readObjectString(object, "repair_feedback_method", event.repairFeedbackMethod);
        readObjectBool(object, "manual_operation", event.manualOperation);
        readObjectString(object, "decision_source", event.decisionSource);
        readObjectBool(object, "training_eligible", event.trainingEligible);
        std::string mode;
        if (readObjectString(object, "mode", mode)) editModeFromName(mode, &event.mode);
        if (readObjectNumber(object, "keep_fraction", number)) event.keepFraction = number;
        if (readObjectNumber(object, "fade_ms", number)) event.fadeMs = number;
        if (readObjectNumber(object, "safety_margin_ms", number)) event.safetyMarginMs = number;
        readObjectString(object, "source", event.source);
        readObjectString(object, "reason", event.reason);
        if (readObjectNumber(object, "dsp_score", number)) event.dspScore = static_cast<float>(number);
        if (readObjectNumber(object, "ai_score", number)) event.aiScore = static_cast<float>(number);
        if (readObjectNumber(object, "ai_max_probability", number)) event.aiMaxProbability = static_cast<float>(number);
        plan->events.push_back(event);
    }
    plan->markers.clear();
    for (const std::string& object : objectArray(json, "markers")) {
        TimelineMarker marker;
        if (readObjectNumber(object, "time", number)) marker.timeSample = static_cast<int64_t>(number);
        if (readObjectNumber(object, "end", number)) marker.endSample = static_cast<int64_t>(number);
        readObjectBool(object, "is_region", marker.isRegion);
        readObjectString(object, "name", marker.name);
        plan->markers.push_back(marker);
    }
    *output = std::move(pending);
    if (error) error->clear();
    return true;
}

bool processFolderReviewed(const std::string& inputDirectory,
                           const std::string& outputDirectory,
                           const std::string& plansDirectory,
                           BatchReviewReport* report,
                           std::string* error) {
    if (!report) {
        if (error) *error = "report batch nullo";
        return false;
    }
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::u8path(inputDirectory);
    const std::filesystem::path plans = std::filesystem::u8path(plansDirectory);
    if (!std::filesystem::is_directory(root, ec) ||
        !std::filesystem::is_directory(plans, ec)) {
        if (error) *error = "cartella input o piani non trovata";
        return false;
    }
    if (!ensureDirectory(outputDirectory, error)) return false;
    const std::filesystem::path outputAbs =
        std::filesystem::absolute(std::filesystem::u8path(outputDirectory), ec).lexically_normal();
    std::vector<std::filesystem::path> files;
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const auto candidate = std::filesystem::absolute(it->path(), ec).lexically_normal();
        if (!ec && it->is_directory(ec)) {
            const std::string dirName = it->path().filename().u8string();
            if (candidate == outputAbs ||
                dirName.rfind("breathclick_out", 0) == 0 ||
                dirName.rfind("breathclick_reviewed", 0) == 0) {
                it.disable_recursion_pending();
                continue;
            }
        }
        if (!it->is_regular_file(ec)) continue;
        if (isSupportedAudioPath(it->path().u8string())) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    report->items.clear();
    report->applied = 0;
    report->skipped = 0;
    for (const auto& file : files) {
        BatchReviewItem item;
        item.inputPath = file.u8string();
        const std::string stem = file.stem().string();
        const std::vector<std::string> candidates = {
            stem + ".plan.json",
            stem + "_reviewed_cut.wav.plan.json",
            stem + "_reviewed_halve.wav.plan.json",
            stem + "_reviewed_attenuate.wav.plan.json",
            stem + "_reviewed.wav.plan.json",
        };
        for (const std::string& candidate : candidates) {
            const auto p = plans / std::filesystem::u8path(candidate);
            if (std::filesystem::exists(p, ec)) { item.planPath = p.u8string(); break; }
        }
        if (item.planPath.empty()) {
            item.status = "da_rivedere";
            item.error = "nessun piano revisionato";
            ++report->skipped;
            report->items.push_back(item);
            continue;
        }
        EditPlan plan;
        std::string localError;
        if (!loadEditPlanJson(item.planPath, &plan, &localError)) {
            item.status = "errore";
            item.error = "piano: " + localError;
            ++report->skipped;
            report->items.push_back(item);
            continue;
        }
        AudioBuffer original;
        AudioFileInfo info;
        if (!readAudio(item.inputPath, original, &info, &localError)) {
            item.status = "errore";
            item.error = "audio: " + localError;
            ++report->skipped;
            report->items.push_back(item);
            continue;
        }
        EditResult result;
        if (!applyEditPlan(original, plan, &result, &localError)) {
            item.status = "errore";
            item.error = "piano: " + localError;
            ++report->skipped;
            report->items.push_back(item);
            continue;
        }
        const std::filesystem::path output = std::filesystem::u8path(outputDirectory) /
            (stem + "_reviewed.wav");
        const WavFormat format = info.isFloat ? WavFormat::Float32 :
            (info.bitsPerSample <= 16 ? WavFormat::Pcm16 : WavFormat::Pcm24);
        if (!writeWavWithMetadata(item.inputPath, output.u8string(), result.audio,
                                  format, &localError, &result.wav)) {
            item.status = "errore";
            item.error = "scrittura: " + localError;
            ++report->skipped;
            report->items.push_back(item);
            continue;
        }
        writeEditReportJson(output.u8string() + ".edit.json", plan, result,
                            output.u8string(), &localError);
        item.outputPath = output.u8string();
        item.status = "applicato";
        ++report->applied;
        report->items.push_back(item);
    }
    return true;
}

bool writeAnalysisJson(const std::string& path, const Analysis& analysis,
                       std::string* error) {
    if (sameFilePath(path, analysis.inputPath)) {
        if (error) *error = "il report o piano non puo' sovrascrivere l'audio originale";
        return false;
    }
    std::ostringstream out;
    out << std::setprecision(17);
    out << "{\n  \"schema\": 1,\n"
        << "  \"file\": \"" << jsonEscape(analysis.inputPath) << "\",\n"
        << "  \"sha256\": \"" << analysis.inputSha256 << "\",\n"
        << "  \"sample_rate\": " << analysis.sampleRate << ",\n"
        << "  \"channels\": " << analysis.channels << ",\n"
        << "  \"bits\": " << analysis.bitsPerSample << ",\n"
        << "  \"float\": " << (analysis.isFloat ? "true" : "false") << ",\n"
        << "  \"frames\": " << analysis.frames << ",\n"
        << "  \"duration_s\": " << std::setprecision(6) << analysis.durationSeconds << ",\n"
        << "  \"detector\": \"" << jsonEscape(analysis.detector) << "\",\n"
        << "  \"ai_inference_frames\": " << analysis.aiInferenceFrames << ",\n";
    if (!analysis.aiProbabilities.empty()) {
        out << "  \"ai_probability_hop_s\": 0.01,\n  \"ai_probabilities\": [" << std::setprecision(9);
        for (size_t i=0;i<analysis.aiProbabilities.size();++i) out << (i?",":"") << analysis.aiProbabilities[i];
        out << "],\n";
    }
    out << "  \"events\": [\n";
    for (size_t i = 0; i < analysis.events.size(); ++i) {
        const Event& e = analysis.events[i];
        out << "    {\"start_s\": " << std::setprecision(6)
            << static_cast<double>(e.startSample) / analysis.sampleRate
            << ", \"end_s\": " << static_cast<double>(e.endSample) / analysis.sampleRate
            << ", \"kind\": " << static_cast<int>(e.kind)
            << ", \"score\": " << e.score
            << ", \"enabled\": " << (e.enabled ? "true" : "false")
            << ", \"state\": \"" << jsonEscape(e.state) << "\""
            << ", \"classification\": \"" << jsonEscape(e.classification) << "\""
            << ", \"decision_source\": \"" << jsonEscape(e.decisionSource) << "\""
            << ", \"training_eligible\": " << (e.trainingEligible ? "true" : "false")
            << ", \"source\": \"" << jsonEscape(e.source) << "\""
            << ", \"reason\": \"" << jsonEscape(e.reason) << "\""
            << ", \"dsp_score\": " << e.dspScore
            << ", \"ai_score\": " << e.aiScore
            << ", \"ai_max_probability\": " << e.aiMaxProbability << "}";
        if (i + 1 < analysis.events.size()) out << ",";
        out << "\n";
    }
    out << "  ],\n  \"excluded_events\": [\n";
    for (size_t i = 0; i < analysis.excludedEvents.size(); ++i) {
        const Event& e = analysis.excludedEvents[i];
        out << "    {\"start_s\": " << static_cast<double>(e.startSample) / analysis.sampleRate
            << ", \"end_s\": " << static_cast<double>(e.endSample) / analysis.sampleRate
            << ", \"source\": \"" << jsonEscape(e.source) << "\""
            << ", \"reason\": \"" << jsonEscape(e.reason) << "\""
            << ", \"dsp_score\": " << e.dspScore
            << ", \"ai_max_probability\": " << e.aiMaxProbability << "}";
        if (i + 1 < analysis.excludedEvents.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n}\n";
    if (!validJsonDocument(out.str())) {
        if (error) *error = "dati del report o piano non validi";
        return false;
    }
    return writeTextAtomic(path, out.str(), error);
}

bool writeReportJson(const std::string& path, const Report& report,
                     std::string* error) {
    if (sameFilePath(path, report.analysis.inputPath)) {
        if (error) *error = "il report o piano non puo' sovrascrivere l'audio originale";
        return false;
    }
    std::ostringstream out;
    out << std::setprecision(17);
    out << "{\n  \"schema\": 1,\n"
        << "  \"input\": \"" << jsonEscape(report.analysis.inputPath) << "\",\n"
        << "  \"input_sha256\": \"" << report.analysis.inputSha256 << "\",\n"
        << "  \"output\": \"" << jsonEscape(report.outputPath) << "\",\n"
        << "  \"output_sha256\": \"" << report.outputSha256 << "\",\n"
        << "  \"detector\": \"" << jsonEscape(report.analysis.detector) << "\",\n"
        << "  \"sample_rate\": " << report.analysis.sampleRate << ",\n"
        << "  \"channels\": " << report.analysis.channels << ",\n"
        << "  \"duration_s\": " << std::setprecision(6) << report.analysis.durationSeconds << ",\n"
        << "  \"reduction_db\": " << report.settings.reductionDb << ",\n"
        << "  \"fade_ms\": " << report.settings.fadeMs << ",\n"
        << "  \"processed_events\": " << report.processedEvents << ",\n"
        << "  \"skipped_events\": " << report.skippedEvents << ",\n"
        << "  \"outside_max_delta_db\": " << std::setprecision(6) << report.outsideMaxDeltaDb << ",\n"
        << "  \"outside_mean_delta_db\": " << report.outsideMeanDeltaDb << ",\n"
        << "  \"processing_s\": " << report.processingSeconds << ",\n"
        << "  \"wav_padding\": " << (report.wav.dataPadding ? "true" : "false")
        << ",\n  \"riff_size_patched\": " << (report.wav.riffSizePatched ? "true" : "false")
        << ",\n  \"click_decisions\": [\n";
    for(size_t i=0;i<report.clickDecisions.size();++i) {
        const auto& op=report.clickDecisions[i];
        out << "    {\"start\": " << op.event.startSample << ", \"end\": " << op.event.endSample
            << ", \"classification\": \"" << jsonEscape(op.event.classification) << "\""
            << ", \"approved\": " << (op.event.approved?"true":"false")
            << ", \"applied\": " << (op.applied?"true":"false")
            << ", \"declined\": " << (op.declined?"true":"false")
            << ", \"outcome\": \"" << operationOutcome(op) << "\", \"click_strategy\": ";
        writeClickDiagnostics(out,op.clickRepair);out << "}" << (i+1<report.clickDecisions.size()?",":"") << "\n";
    }
    out << "  ],\n  \"events\": [\n";
    for (size_t i = 0; i < report.metrics.size(); ++i) {
        const EventMetric& m = report.metrics[i];
        out << "    {\"start_s\": " << static_cast<double>(m.startSample) / report.analysis.sampleRate
            << ", \"end_s\": " << static_cast<double>(m.endSample) / report.analysis.sampleRate
            << ", \"rms_before_db\": " << m.rmsBeforeDb
            << ", \"rms_after_db\": " << m.rmsAfterDb
            << ", \"delta_db\": " << m.deltaDb << "}";
        if (i + 1 < report.metrics.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n}\n";
    if (!validJsonDocument(out.str())) {
        if (error) *error = "dati del report o piano non validi";
        return false;
    }
    return writeTextAtomic(path, out.str(), error);
}

bool writeReportText(const std::string& path, const Report& report,
                     std::string* error) {
    if (sameFilePath(path, report.analysis.inputPath)) {
        if (error) *error = "il report o piano non puo' sovrascrivere l'audio originale";
        return false;
    }
    std::ostringstream out;
    out << std::setprecision(17);
    out << "NL Breath Click Studio - report di elaborazione\n";
    out << "input: " << report.analysis.inputPath << "\n";
    out << "output: " << report.outputPath << "\n";
    out << "formato: " << report.analysis.sampleRate << " Hz, "
        << report.analysis.channels << " canali, "
        << report.analysis.bitsPerSample << " bit\n";
    out << "rilevatore: " << report.analysis.detector << "\n";
    out << "eventi applicati: " << report.processedEvents
        << ", saltati: " << report.skippedEvents << "\n";
    out << "attenuazione nominale: " << report.settings.reductionDb << " dB, fade "
        << report.settings.fadeMs << " ms\n";
    out << "scostamento fuori intervalli: medio " << report.outsideMeanDeltaDb
        << " dB, massimo " << report.outsideMaxDeltaDb << " dB\n\n";
    for (size_t i = 0; i < report.metrics.size(); ++i) {
        const EventMetric& m = report.metrics[i];
        out << "evento " << (i + 1) << ": "
            << std::fixed << std::setprecision(3)
            << static_cast<double>(m.startSample) / report.analysis.sampleRate
            << " - " << static_cast<double>(m.endSample) / report.analysis.sampleRate
            << " s, RMS " << m.rmsBeforeDb << " -> " << m.rmsAfterDb
            << " dB (" << m.deltaDb << " dB)\n";
    }
    return writeTextAtomic(path, out.str(), error);
}

} // namespace studio
} // namespace bc
