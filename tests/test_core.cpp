// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  test_core.cpp — Test unitari del motore
//
//  Ogni prova verifica un comportamento misurabile, non la semplice assenza di
//  crash. I segnali di riferimento sono generati con verita' a terra nota.
// ============================================================================
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "core/detect/offline_analyzer.h"
#include "core/dsp/click_detector.h"
#include "core/dsp/frame_features.h"
#include "core/dsp/resampler.h"
#include "core/edit/region_editor.h"
#include "core/edit/wav_io.h"
#include "shared/synthetic_audio.h"

using namespace bc;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what) {
    std::cout << "  [" << (condition ? " OK " : "FAIL") << "] " << what << "\n";
    if (!condition) ++g_failures;
}

void section(const char* name) {
    std::cout << "\n" << name << "\n" << std::string(std::strlen(name), '-') << "\n";
}

// ---------------------------------------------------------------------------
//  FrameAnalyzer
// ---------------------------------------------------------------------------
void testFrameAnalyzer() {
    section("FrameAnalyzer");

    FrameAnalyzer::Config config;
    config.fftSize = 1024;
    config.sampleRate = 48000.0f;
    FrameAnalyzer analyzer(config);

    // Silenzio: livello molto basso, nessuna armonicita'.
    std::vector<float> silence(1024, 0.0f);
    const FrameFeatures silent = analyzer.analyze(silence.data(), silence.size());
    check(silent.rms < 1.0e-6f, "silenzio: RMS praticamente nullo");
    check(silent.levelDb < -100.0f, "silenzio: livello in dB molto basso");

    analyzer.reset();

    // Tono puro a 440 Hz: spettro concentrato => flatness bassa, armonicita' alta.
    std::vector<float> tone(1024, 0.0f);
    for (size_t i = 0; i < tone.size(); ++i) {
        tone[i] = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 48000.0f);
    }
    const FrameFeatures tonal = analyzer.analyze(tone.data(), tone.size());
    check(tonal.rms > 0.3f, "tono: RMS plausibile");
    check(tonal.spectralFlatness < 0.15f, "tono: flatness spettrale bassa");
    check(tonal.harmonicRatio > 0.7f, "tono: segnale riconosciuto come periodico");
    check(std::abs(tonal.pitchHz - 440.0f) < 25.0f,
          "tono: frequenza fondamentale stimata vicina a 440 Hz (trovata " +
              std::to_string(static_cast<int>(tonal.pitchHz)) + " Hz)");

    analyzer.reset();

    // Rumore bianco: spettro piatto => flatness alta, armonicita' bassa.
    std::vector<float> noise(1024, 0.0f);
    unsigned state = 12345u;
    for (size_t i = 0; i < noise.size(); ++i) {
        state = state * 1664525u + 1013904223u;
        noise[i] = (static_cast<float>((state >> 9) & 0x7FFFFF) / 4194304.0f - 1.0f) * 0.4f;
    }
    const FrameFeatures noisy = analyzer.analyze(noise.data(), noise.size());
    check(noisy.spectralFlatness > 0.30f, "rumore: flatness spettrale alta");
    check(noisy.harmonicRatio < 0.35f, "rumore: salienza armonica bassa");
    check(noisy.highBandRatio > tonal.highBandRatio,
          "rumore: piu' energia in alta frequenza di un tono puro (" +
              std::to_string(noisy.highBandRatio) + " contro " +
              std::to_string(tonal.highBandRatio) + ")");
}

// ---------------------------------------------------------------------------
//  Ricampionatore
// ---------------------------------------------------------------------------
void testResampler() {
    section("Ricampionatore");

    LinearResampler resampler;
    check(resampler.configure(48000.0, 16000.0), "configurazione 48k -> 16k");
    check(std::abs(resampler.ratio() - (1.0 / 3.0)) < 1.0e-9, "rapporto di conversione corretto");

    // Tono a 1 kHz: dopo il ricampionamento la frequenza non deve cambiare.
    const size_t inputCount = 48000;
    std::vector<float> input(inputCount);
    for (size_t i = 0; i < inputCount; ++i) {
        input[i] = std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / 48000.0f);
    }

    std::vector<float> output;
    resampler.process(input.data(), input.size(), output);

    const size_t expected = inputCount / 3;
    check(output.size() >= expected - 4 && output.size() <= expected + 4,
          "numero di campioni prodotti corretto (attesi ~" + std::to_string(expected) +
              ", ottenuti " + std::to_string(output.size()) + ")");

    // Conteggio degli attraversamenti dello zero sul tratto centrale: deve
    // corrispondere a 1 kHz a 16 kHz, cioe' 2000 attraversamenti al secondo.
    int crossings = 0;
    const size_t from = output.size() / 4;
    const size_t to = output.size() * 3 / 4;
    for (size_t i = from + 1; i < to; ++i) {
        if ((output[i] >= 0.0f) != (output[i - 1] >= 0.0f)) ++crossings;
    }
    const double seconds = static_cast<double>(to - from) / 16000.0;
    const double estimatedHz = static_cast<double>(crossings) / (2.0 * seconds);
    check(std::abs(estimatedHz - 1000.0) < 60.0,
          "frequenza conservata dopo il ricampionamento (" +
              std::to_string(static_cast<int>(estimatedHz)) + " Hz)");
}

// ---------------------------------------------------------------------------
//  Rilevatore di click: comportamento su un impulso isolato
// ---------------------------------------------------------------------------
void testClickDetectorOnImpulse() {
    section("Rilevatore click");

    ClickDetectorConfig config;
    config.sampleRate = 48000.0f;
    config.subWindowSamples = 64;
    ClickDetector detector(config);

    const int subWindow = detector.subWindowSamples();

    // Fase 1: voce sintetica con rumore di fondo, nessun click.
    // La storia del rilevatore registra il picco di |d| di ogni sotto-finestra:
    // qui si costruisce un fondo di rumore realistico rispetto al quale un
    // click deve risultare anomalo.
    std::vector<float> block(static_cast<size_t>(subWindow));
    float maximumScore = 0.0f;
    unsigned state = 999u;

    auto fillQuietVoice = [&](int blockIndex, float noiseAmplitude) {
        for (int i = 0; i < subWindow; ++i) {
            const float t = static_cast<float>(blockIndex * subWindow + i) / 48000.0f;
            state = state * 1664525u + 1013904223u;
            const float noise =
                (static_cast<float>((state >> 9) & 0x7FFFFF) / 4194304.0f - 1.0f) * noiseAmplitude;
            block[static_cast<size_t>(i)] =
                0.3f * std::sin(2.0f * 3.14159265f * 180.0f * t) + noise;
        }
    };

    for (int index = 0; index < 120; ++index) {
        fillQuietVoice(index, 0.004f);
        const ClickScore score = detector.processBlock(block.data(), block.size());
        maximumScore = std::max(maximumScore, score.score);
    }
    check(maximumScore < 0.30f,
          "nessun falso positivo su voce e rumore (punteggio massimo " +
              std::to_string(maximumScore) + ")");

    // Fase 2: il fondo resta lo stesso e si inserisce un impulso netto, di
    // ampiezza paragonabile a quella di un click reale.
    float clickScore = 0.0f;
    for (int index = 120; index < 160; ++index) {
        fillQuietVoice(index, 0.004f);
        if (index == 130) {
            // Impulso di 3 campioni: la sagoma tipica di un click digitale.
            block[10] += 0.55f;
            block[11] -= 0.55f;
            block[12] += 0.55f;
        }
        const ClickScore score = detector.processBlock(block.data(), block.size());
        clickScore = std::max(clickScore, score.score);
    }
    check(clickScore > 0.80f,
          "impulso rilevato con confidenza alta (punteggio " +
              std::to_string(clickScore) + ")");
}

// ---------------------------------------------------------------------------
//  Analisi completa su materiale con verita' a terra
// ---------------------------------------------------------------------------
struct MatchResult {
    size_t matched = 0;
    size_t missed = 0;
    size_t falsePositives = 0;
};

MatchResult matchKind(const bc::synthetic::TestTake& take,
                      const AnalysisResult& result,
                      DetectionKind kind,
                      double toleranceMs) {
    MatchResult outcome;
    std::vector<bool> used(result.detections.size(), false);
    const double toleranceSamples = toleranceMs * take.sampleRate / 1000.0;

    for (const bc::synthetic::GroundTruthEvent& event : take.events) {
        if (event.kind != kind) continue;

        bool found = false;
        for (size_t i = 0; i < result.detections.size(); ++i) {
            if (used[i] || result.detections[i].kind != kind) continue;
            const double center = 0.5 * static_cast<double>(event.startSample + event.endSample);
            const double detected =
                0.5 * static_cast<double>(result.detections[i].startSample +
                                          result.detections[i].endSample);
            if (std::abs(center - detected) <= toleranceSamples) {
                used[i] = true;
                found = true;
                break;
            }
        }
        if (found) ++outcome.matched; else ++outcome.missed;
    }

    for (size_t i = 0; i < result.detections.size(); ++i) {
        if (!used[i] && result.detections[i].kind == kind) ++outcome.falsePositives;
    }
    return outcome;
}

void testOfflineAnalysis() {
    section("Analisi offline con verita' a terra");

    const bc::synthetic::TestTake take = bc::synthetic::makeVocalTake();

    DetectorSettings settings;
    OfflineAnalyzer analyzer;

    const AnalysisResult result = analyzer.analyze(take.samples.data(),
                                                   take.samples.size(),
                                                   take.sampleRate,
                                                   settings);

    check(result.error.empty(), "analisi completata senza errori");
    check(result.framesAnalyzed > 0, "finestre analizzate: " +
                                         std::to_string(result.framesAnalyzed));

    std::cout << "        gate vocale: " << (result.vadActive ? result.vadStatus : "assente")
              << "  |  rilevamenti: " << result.countOf(DetectionKind::Breath) << " respiri, "
              << result.countOf(DetectionKind::Click) << " click"
              << "  |  attesi: " << take.countOf(DetectionKind::Breath) << " respiri, "
              << take.countOf(DetectionKind::Click) << " click\n";
    std::cout << "        fondo rumore: " << result.noiseFloorDb << " dB\n";

    const MatchResult breaths =
        matchKind(take, result, DetectionKind::Breath, 120.0);
    const MatchResult clicks =
        matchKind(take, result, DetectionKind::Click, 30.0);

    const size_t truthBreaths = take.countOf(DetectionKind::Breath);
    const size_t truthClicks = take.countOf(DetectionKind::Click);

    check(breaths.matched == truthBreaths,
          "respiri trovati: " + std::to_string(breaths.matched) + "/" +
              std::to_string(truthBreaths));
    check(breaths.falsePositives == 0,
          "nessun respiro falso (trovati " + std::to_string(breaths.falsePositives) + ")");
    check(clicks.matched == truthClicks,
          "click trovati: " + std::to_string(clicks.matched) + "/" +
              std::to_string(truthClicks));
    check(clicks.falsePositives == 0,
          "nessun click falso (trovati " + std::to_string(clicks.falsePositives) + ")");

    // Silenzio: nessun rilevamento.
    const bc::synthetic::TestTake silence = bc::synthetic::makeSilenceTake(2.0, 48000.0, 5u);
    const AnalysisResult silenceResult = analyzer.analyze(silence.samples.data(),
                                                         silence.samples.size(),
                                                         silence.sampleRate,
                                                         settings);
    check(silenceResult.detections.empty(),
          "sul silenzio non viene rilevato nulla (trovati " +
              std::to_string(silenceResult.detections.size()) + ")");

    // Solo voce: nessun respiro.
    const bc::synthetic::TestTake voice = bc::synthetic::makeVoiceOnlyTake();
    const AnalysisResult voiceResult = analyzer.analyze(voice.samples.data(),
                                                        voice.samples.size(),
                                                        voice.sampleRate,
                                                        settings);
    check(voiceResult.countOf(DetectionKind::Breath) == 0,
          "la voce da sola non viene scambiata per respiro");
}

// ---------------------------------------------------------------------------
//  Correzione audio
// ---------------------------------------------------------------------------
void testRegionEditor() {
    section("Correzione delle regioni");

    const bc::synthetic::TestTake take = bc::synthetic::makeVocalTake();

    DetectorSettings settings;
    OfflineAnalyzer analyzer;
    const AnalysisResult result = analyzer.analyze(take.samples.data(),
                                                   take.samples.size(),
                                                   take.sampleRate,
                                                   settings);

    std::vector<float> processed = take.samples;

    EditSettings edit;
    edit.breathReductionDb = -18.0f;

    const EditReport report = RegionEditor::apply(processed,
                                                  1,
                                                  take.sampleRate,
                                                  result.detections,
                                                  edit);

    check(report.breathRegions > 0, "regioni di respiro elaborate: " +
                                        std::to_string(report.breathRegions));

    // Energia dentro le regioni di respiro: deve calare in modo netto.
    double before = 0.0;
    double after = 0.0;
    for (const Detection& detection : result.detections) {
        if (detection.kind != DetectionKind::Breath) continue;
        for (int64_t i = detection.startSample; i < detection.endSample; ++i) {
            if (i < 0 || i >= static_cast<int64_t>(take.samples.size())) continue;
            before += static_cast<double>(take.samples[static_cast<size_t>(i)]) *
                      take.samples[static_cast<size_t>(i)];
            after += static_cast<double>(processed[static_cast<size_t>(i)]) *
                     processed[static_cast<size_t>(i)];
        }
    }
    const double reductionDb =
        (before > 1.0e-12 && after > 1.0e-12) ? 10.0 * std::log10(after / before) : 0.0;
    check(reductionDb < -8.0,
          "energia dei respiri ridotta ( " + std::to_string(reductionDb) + " dB)");

    // Fuori dalle regioni il segnale non deve cambiare (nessun effetto collaterale).
    float maximumOutsideDelta = 0.0f;
    for (int64_t i = 0; i < static_cast<int64_t>(take.samples.size()); ++i) {
        bool insideRegion = false;
        for (const Detection& detection : result.detections) {
            // Il margine considera le rampe di attacco e rilascio.
            const int64_t margin = static_cast<int64_t>(take.sampleRate * 0.12);
            if (i >= detection.startSample - margin && i < detection.endSample + margin) {
                insideRegion = true;
                break;
            }
        }
        if (!insideRegion) {
            maximumOutsideDelta = std::max(
                maximumOutsideDelta,
                std::abs(processed[static_cast<size_t>(i)] - take.samples[static_cast<size_t>(i)]));
        }
    }
    check(maximumOutsideDelta == 0.0f,
          "fuori dalle regioni il segnale e' intatto (delta massimo " +
              std::to_string(maximumOutsideDelta) + ")");

    // La riparazione dei click non deve introdurre discontinuita' evidenti.
    std::vector<float> repaired = take.samples;
    EditSettings repairSettings;
    repairSettings.repairClicks = true;
    // Explicit synthetic reference review. Automatic processing deliberately
    // leaves ambiguous speech-overlap candidates alone; repair quality must
    // be measured after approving the known injected clicks, not by forcing
    // every automatic proposal through the conservative policy.
    std::vector<bc::RegionOperation> reviewed;
    for(const auto& detection:result.detections) {
        if(detection.kind!=DetectionKind::Click)continue;
        bool injected=false;
        for(const auto& truth:take.events)
            if(truth.kind==DetectionKind::Click && detection.startSample<truth.endSample && detection.endSample>truth.startSample)injected=true;
        if(!injected)continue;
        bc::RegionOperation operation;operation.kind=DetectionKind::Click;
        operation.startSample=detection.startSample;operation.endSample=detection.endSample;operation.repair=true;
        operation.repairMethod="predictive"; // Explicit reference benchmark, separate from safe-auto refusal.
        reviewed.push_back(operation);
    }
    RegionEditor::applyOperations(repaired, 1, take.sampleRate, reviewed, repairSettings);

    float maximumJump = 0.0f;
    for (size_t i = 1; i < repaired.size(); ++i) {
        maximumJump = std::max(maximumJump, std::abs(repaired[i] - repaired[i - 1]));
    }
    check(maximumJump < 0.8f,
          "nessun salto campione brusco dopo la riparazione (max " +
              std::to_string(maximumJump) + ")");
}

// ---------------------------------------------------------------------------
//  Lettura e scrittura WAV
// ---------------------------------------------------------------------------
void testWavRoundTrip() {
    section("Ingresso/uscita WAV");

    AudioBuffer original;
    original.channels = 2;
    original.sampleRate = 44100;
    original.samples.resize(44100);

    for (size_t i = 0; i < original.samples.size(); ++i) {
        original.samples[i] = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f *
                                              static_cast<float>(i) / 44100.0f);
    }

    struct FormatCase {
        WavFormat format;
        const char* name;
        float tolerance;
    };

    const FormatCase cases[] = {
        {WavFormat::Pcm16, "PCM 16 bit", 0.001f},
        {WavFormat::Pcm24, "PCM 24 bit", 0.0001f},
        {WavFormat::Float32, "float 32 bit", 1.0e-6f}};

    for (const FormatCase& testCase : cases) {
        const std::string path = std::string("bc_test_roundtrip_") + testCase.name + ".wav";

        std::string error;
        const bool written = writeWav(path, original, testCase.format, &error);
        check(written, std::string("scrittura ") + testCase.name + (written ? "" : ": " + error));

        AudioBuffer readBack;
        const bool read = readWav(path, readBack, &error);
        check(read, std::string("rilettura ") + testCase.name + (read ? "" : ": " + error));

        if (written && read) {
            check(readBack.channels == original.channels &&
                      readBack.sampleRate == original.sampleRate,
                  std::string("intestazione conservata (") + testCase.name + ")");

            float maximumError = 0.0f;
            const size_t count = std::min(readBack.samples.size(), original.samples.size());
            for (size_t i = 0; i < count; ++i) {
                maximumError = std::max(maximumError,
                                        std::abs(readBack.samples[i] - original.samples[i]));
            }
            check(maximumError <= testCase.tolerance,
                  std::string("campioni conservati entro la tolleranza (") + testCase.name +
                      ", errore " + std::to_string(maximumError) + ")");
        }

        std::remove(path.c_str());
    }

    // Downmix mono.
    AudioBuffer mono = original;
    const std::vector<float> monoView = mono.toMono();
    check(monoView.size() == static_cast<size_t>(original.frameCount()),
          "downmix mono: numero di campioni corretto");
}

// ---------------------------------------------------------------------------
//  Dispositivo di calcolo
//
//  Il dispositivo non si sceglie piu': ONNX Runtime e' stato rimosso e il
//  rilevamento e' tutto DSP su CPU. Resta una sola cosa da verificare, ed e'
//  che non ci sia piu' nulla di variabile da cui dipendere.
// ---------------------------------------------------------------------------
void testDevicePolicy() {
    section("Dispositivo di calcolo");

    DeviceRequest request = DeviceRequest::Gpu;
    check(parseDeviceRequest("gpu", request) && request == DeviceRequest::Cpu,
          "qualunque valore richiesto si ottiene CPU: e' l'unico dispositivo");

    const DeviceStatus status;
    check(status.shortLabel() == "CPU", "etichetta CPU");
    check(!status.usingGpu(), "non esiste un percorso GPU");
    check(!status.fellBackToCpu, "non c'e' nulla da cui ripiegare");
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::cout << "Test del motore BreathClick\n";
    std::cout << "===========================\n";

    testFrameAnalyzer();
    testResampler();
    testClickDetectorOnImpulse();
    testOfflineAnalysis();
    testRegionEditor();
    testWavRoundTrip();
    testDevicePolicy();

    std::cout << "\n===========================\n";
    if (g_failures == 0) {
        std::cout << "TUTTI I TEST SUPERATI\n";
        return 0;
    }
    std::cout << "TEST FALLITI: " << g_failures << "\n";
    return 1;
}
