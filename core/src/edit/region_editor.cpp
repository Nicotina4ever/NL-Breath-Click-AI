// SPDX-License-Identifier: MPL-2.0
#include "core/edit/region_editor.h"
#include "core/dsp/click_repair.h"

#include <algorithm>
#include <cmath>

namespace bc {

namespace {

float dbToGain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

float gainToDb(float gain) {
    return gain > 1.0e-6f ? 20.0f * std::log10(gain) : -120.0f;
}

// Rampa a coseno rialzato: nessuna discontinuita' nella derivata, quindi
// nessun artefatto udibile.
float raisedCosine(float t) {
    const float clamped = std::max(0.0f, std::min(1.0f, t));
    return 0.5f * (1.0f - std::cos(3.14159265358979323846f * clamped));
}

int64_t msToSamples(double ms, double sampleRate) {
    if (ms <= 0.0) return 0;
    return static_cast<int64_t>(std::llround(ms * sampleRate / 1000.0));
}

// Una regione viene riparata solo se e' un click, se l'operazione lo richiede e
// se la durata resta entro il limite: oltre, l'interpolazione si sente.
bool shouldRepair(const RegionOperation& operation, double sampleRate, const EditSettings& settings) {
    if (!operation.repair || operation.kind != DetectionKind::Click) {
        return false;
    }
    const double lengthMs = 1000.0 * static_cast<double>(operation.lengthSamples()) / sampleRate;
    return lengthMs <= settings.repairMaxMs;
}

} // namespace

RangeGainEnvelope::RangeGainEnvelope(int64_t start, int64_t end, int64_t fadeFrames, float targetGain)
    : start_(start), end_(end),
      fade_(std::max<int64_t>(0, std::min(fadeFrames, (end - start) / 2))), target_(targetGain) {}

float RangeGainEnvelope::gainAt(int64_t frame) const {
    if (frame < start_ || frame >= end_) return 1;
    constexpr double pi = 3.14159265358979323846;
    if (frame < start_ + fade_) {
        const double t = static_cast<double>(frame - start_ + 1) / fade_;
        return target_ + (1 - target_) * static_cast<float>(0.5 * (1 + std::cos(pi * t)));
    }
    if (frame >= end_ - fade_) {
        const double t = static_cast<double>(frame - (end_ - fade_)) / fade_;
        return target_ + (1 - target_) * static_cast<float>(0.5 * (1 - std::cos(pi * t)));
    }
    return target_;
}

std::vector<RegionOperation> RegionEditor::regionOperationsFromDetections(
    const std::vector<Detection>& detections,
    const EditSettings& settings) {
    std::vector<RegionOperation> operations;
    operations.reserve(detections.size());

    for (const Detection& detection : detections) {
        if(detection.kind==DetectionKind::Click && clickReviewRequired(detection.classification))continue;
        RegionOperation operation;
        operation.kind = detection.kind;
        operation.startSample = detection.startSample;
        operation.endSample = detection.endSample;
        operation.reductionDb = detection.kind == DetectionKind::Breath
                                    ? settings.breathReductionDb
                                    : settings.clickReductionDb;
        operation.repair = settings.repairClicks;
        operations.push_back(operation);
    }

    return operations;
}

std::vector<float> RegionEditor::buildGainCurve(int64_t frameCount,
                                                double sampleRate,
                                                const std::vector<RegionOperation>& operations,
                                                const EditSettings& settings) {
    std::vector<float> gain;
    if (frameCount <= 0) return gain;

    gain.assign(static_cast<size_t>(frameCount), 1.0f);

    const int64_t attack = msToSamples(settings.attackMs, sampleRate);
    const int64_t release = msToSamples(settings.releaseMs, sampleRate);
    const int64_t prePad = msToSamples(settings.prePadMs, sampleRate);
    const int64_t postPad = msToSamples(settings.postPadMs, sampleRate);

    for (const RegionOperation& operation : operations) {
        // I click riparati per interpolazione non producono attenuazione.
        if(operation.kind==DetectionKind::Click && operation.repair && operation.repairMethod=="none")continue;
        if (shouldRepair(operation, sampleRate, settings)) {
            continue;
        }

        const float targetGain = dbToGain(std::min(0.0f, operation.reductionDb));
        if (targetGain >= 0.9999f) {
            continue;   // attenuazione nulla: niente da fare
        }

        const int64_t flatStart = std::max<int64_t>(0, operation.startSample - prePad);
        const int64_t flatEnd = std::min<int64_t>(frameCount, operation.endSample + postPad);
        if (flatEnd <= flatStart) continue;

        const int64_t rampStart = std::max<int64_t>(0, flatStart - attack);
        const int64_t rampEnd = std::min<int64_t>(frameCount, flatEnd + release);

        const int64_t attackLength = flatStart - rampStart;
        const int64_t releaseLength = rampEnd - flatEnd;

        for (int64_t i = rampStart; i < rampEnd; ++i) {
            float value = targetGain;

            if (attackLength > 0 && i < flatStart) {
                const float t = static_cast<float>(i - rampStart) /
                                static_cast<float>(attackLength);
                value = 1.0f + (targetGain - 1.0f) * raisedCosine(t);
            } else if (releaseLength > 0 && i >= flatEnd) {
                const float t = static_cast<float>(i - flatEnd) /
                                static_cast<float>(releaseLength);
                value = targetGain + (1.0f - targetGain) * raisedCosine(t);
            }

            // Regioni sovrapposte: prevale l'attenuazione maggiore.
            gain[static_cast<size_t>(i)] = std::min(gain[static_cast<size_t>(i)], value);
        }
    }

    return gain;
}

std::vector<float> RegionEditor::buildGainCurve(int64_t frameCount,
                                                double sampleRate,
                                                const std::vector<Detection>& detections,
                                                const EditSettings& settings) {
    return buildGainCurve(frameCount, sampleRate,
                          regionOperationsFromDetections(detections, settings),
                          settings);
}

EditReport RegionEditor::applyOperations(std::vector<float>& interleaved,
                                         int channels,
                                         double sampleRate,
                                         const std::vector<RegionOperation>& operations,
                                         const EditSettings& settings) {
    EditReport report;
    if (interleaved.empty() || channels <= 0 || sampleRate <= 0.0) {
        return report;
    }

    const int64_t frameCount = static_cast<int64_t>(interleaved.size()) / channels;

    // ---- 1. riparazione dei click -------------------------------------------
    // Effettuata PRIMA dell'attenuazione: l'interpolazione lavora sul segnale
    // originale, che e' cio' che serve per ricostruire il campione mancante.
    std::vector<std::pair<int64_t,std::vector<float>>> patches;
    for (const RegionOperation& operation : operations) {
        if (operation.kind != DetectionKind::Click) continue;
        if (!operation.repair) continue;
        if(operation.repairMethod=="none") {
            ClickRepairRecord record;record.operation=operation;
            record.status=ClickRepairStatus::Bypassed;record.diagnostics.requestedMethod="none";
            record.diagnostics.reason="manual_bypass";report.clickDecisions.push_back(record);continue;
        }

        if (!shouldRepair(operation, sampleRate, settings)) {
            continue;   // verra' attenuato al passo 2
        }

        const int64_t start = std::max<int64_t>(1, operation.startSample);
        const int64_t end = std::min<int64_t>(frameCount - 1, operation.endSample);
        if (end <= start) {
            ++report.clickRegionsSkipped;
            continue;
        }

        const int64_t context=static_cast<int64_t>(std::ceil(sampleRate*.025));
        const int64_t from=std::max<int64_t>(0,start-context),to=std::min(frameCount,end+context);
        std::vector<float> local(interleaved.begin()+from*channels,interleaved.begin()+to*channels);
        ClickRepairDiagnostics diagnostics;
        const auto status=repairClick(local, channels, sampleRate, start-from, end-from, operation.repairMethod,&diagnostics);
        report.clickDecisions.push_back({operation,diagnostics,status});
        if(status==ClickRepairStatus::Declined){report.declinedClicks.push_back(operation);continue;}
        if (status==ClickRepairStatus::Invalid) {
            ++report.clickRegionsSkipped;
            continue;
        }
        patches.push_back({start,std::vector<float>(local.begin()+(start-from)*channels,local.begin()+(end-from)*channels)});
        report.samplesModified += (end - start) * channels;
        ++report.clickRegionsRepaired;
    }
    // Every prediction saw the same input, including adjacent events' context.
    for(const auto& patch:patches)std::copy(patch.second.begin(),patch.second.end(),interleaved.begin()+patch.first*channels);

    // ---- 2. curva di guadagno ----------------------------------------------
    const std::vector<float> gain =
        buildGainCurve(frameCount, sampleRate, operations, settings);

    if (gain.empty()) {
        return report;
    }

    // Contabilita' per il report: i click riparati non compaiono nella curva.
    for (const RegionOperation& operation : operations) {
        if (operation.kind == DetectionKind::Breath) {
            ++report.breathRegions;
        } else if (operation.kind == DetectionKind::Click) {
            if (!shouldRepair(operation, sampleRate, settings)) {
                ++report.clickRegionsAttenuated;
            }
        }
    }

    float minimumGain = 1.0f;
    int64_t modified = 0;

    for (int64_t f = 0; f < frameCount; ++f) {
        const float g = gain[static_cast<size_t>(f)];
        minimumGain = std::min(minimumGain, g);
        if (g >= 0.9999f) {
            continue;
        }
        for (int channel = 0; channel < channels; ++channel) {
            const size_t index = static_cast<size_t>(f * channels + channel);
            interleaved[index] *= g;
        }
        ++modified;
    }

    report.samplesModified += modified * channels;
    report.maxGainReductionDb = gainToDb(minimumGain);

    return report;
}

EditReport RegionEditor::apply(std::vector<float>& interleaved,
                               int channels,
                               double sampleRate,
                               const std::vector<Detection>& detections,
                               const EditSettings& settings) {
    return applyOperations(interleaved, channels, sampleRate,
                           regionOperationsFromDetections(detections, settings),
                           settings);
}

} // namespace bc
